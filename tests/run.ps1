# Regression tests for the CCPL compiler, runtime and foreign function layer.
# Run from the repository root after building build\coolc.exe.
param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
)

$ErrorActionPreference = 'Continue'
$coolc = Join-Path $Root 'build\coolc.exe'
if (-not (Test-Path $coolc)) { throw "build\coolc.exe not found; run build.bat first" }

$work = Join-Path $env:TEMP ('ccpl-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $work | Out-Null
$failed = 0
$passed = 0

function Normalize([string[]]$lines) {
    if ($null -eq $lines) { return '' }
    return (($lines -join "`n").Replace("`r`n", "`n")).TrimEnd("`n")
}

function RunCase([string]$src, [string]$expectedFile, [string]$exeName) {
    $exe = Join-Path $work $exeName
    $build = & $coolc $src -o $exe 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAIL build $src"
        $build | ForEach-Object { Write-Host "  $_" }
        return $false
    }
    $actual = & $exe 2>&1
    $code = $LASTEXITCODE
    $expected = Normalize (Get-Content -LiteralPath $expectedFile)
    $got = Normalize $actual
    if ($code -ne 0) {
        Write-Host "FAIL run $src (exit $code)"
        Write-Host $got
        return $false
    }
    if ($got -ne $expected) {
        Write-Host "FAIL output $src"
        Write-Host "expected:"
        Write-Host $expected
        Write-Host "actual:"
        Write-Host $got
        return $false
    }
    Write-Host "ok $([IO.Path]::GetFileName($src))"
    return $true
}

$casesDir = Join-Path $Root 'tests\cases'
foreach ($src in Get-ChildItem (Join-Path $casesDir '*.ccpl') | Sort-Object Name) {
    if ($src.Name -eq 'ffi_python.ccpl') {
        if (-not (Get-Command python -ErrorAction SilentlyContinue) -and -not $env:CCPL_PYTHON_DLL) {
            Write-Host "skip ffi_python.ccpl (no python found)"
            continue
        }
    }
    $expected = [IO.Path]::ChangeExtension($src.FullName, '.out')
    if (RunCase $src.FullName $expected ($src.BaseName + '.exe')) { $passed++ } else { $failed++ }
}

foreach ($src in Get-ChildItem (Join-Path $Root 'tests\fail\*.ccpl') | Sort-Object Name) {
    $expected = [IO.Path]::ChangeExtension($src.FullName, '.err')
    $exe = Join-Path $work ($src.BaseName + '.exe')
    $out = (& $coolc $src.FullName -o $exe 2>&1 | Out-String)
    $need = (Get-Content -LiteralPath $expected -Raw).Trim()
    if ($LASTEXITCODE -eq 0 -or $out -notlike "*$need*") {
        Write-Host "FAIL expected error $($src.Name)"
        Write-Host $out
        $failed++
    } else {
        Write-Host "ok fail/$($src.Name)"
        $passed++
    }
}

# UTF 8 byte order mark handling.
$bom = Join-Path $work 'bom.ccpl'
[IO.File]::WriteAllBytes($bom, ([byte[]](0xEF,0xBB,0xBF) + [Text.Encoding]::ASCII.GetBytes("say(`"bom ok`")`n")))
if ((& $coolc $bom -o (Join-Path $work 'bom.exe') 2>&1) -and $LASTEXITCODE -eq 0) {
    $got = (& (Join-Path $work 'bom.exe') 2>&1 | Out-String).Trim()
    if ($got -eq 'bom ok') { Write-Host 'ok bom'; $passed++ }
    else { Write-Host "FAIL bom: $got"; $failed++ }
} else { Write-Host 'FAIL bom compile'; $failed++ }

# Optional C++ test. It needs a C++ compiler; the GitHub Windows image has one.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vcvars = $null
if (Test-Path $vswhere) {
    $vsPath = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null)
    if ($vsPath) {
        $candidate = Join-Path $vsPath 'VC\Auxiliary\Build\vcvarsall.bat'
        if (Test-Path $candidate) { $vcvars = $candidate }
    }
}
if ($vcvars) {
    $cpp = Join-Path $Root 'examples\ffi_cpp'
    $cppWork = Join-Path $work 'cpp'
    New-Item -ItemType Directory -Force $cppWork | Out-Null
    $buildLog = cmd /c "call `"$vcvars`" x64 >nul 2>&1 && cd /d `"$cppWork`" && cl /nologo /LD /EHsc /O2 `"$cpp\ffitest.cpp`" 2>&1"
    $dll = Join-Path $cppWork 'ffitest.dll'
    if ($LASTEXITCODE -eq 0 -and (Test-Path $dll)) {
        $exe = Join-Path $cppWork 'cpptest.exe'
        $built = & $coolc (Join-Path $cpp 'use_ffi.ccpl') -o $exe 2>&1
        if ($LASTEXITCODE -eq 0) {
            $actual = Normalize (& $exe 2>&1)
            $expected = Normalize @(
                "c_add:`t42",
                "c_greet:`thello, ccpl",
                "c++ free function:`t5",
                "c++ member function:`t10"
            )
            if ($actual -eq $expected) { Write-Host 'ok ffi_cpp'; $passed++ }
            else { Write-Host "FAIL ffi_cpp"; Write-Host $actual; $failed++ }
        } else {
            Write-Host 'FAIL ffi_cpp compile'
            $built | ForEach-Object { Write-Host $_ }
            $failed++
        }
    } else {
        Write-Host 'skip ffi_cpp (C++ build failed)'
    }
} else {
    Write-Host 'skip ffi_cpp (no Visual C++ tools)'
}

# Package manager smoke test against a local file registry.
$clmBuilt = Join-Path $Root 'build\clm.exe'
if (Test-Path $clmBuilt) {
    $reg = Join-Path $work 'reg'
    $pkg = Join-Path $reg 'pkgroot\demo'
    $target = Join-Path $work 'clmroot'
    New-Item -ItemType Directory -Force $pkg, (Join-Path $reg 'packages\demo'), $target | Out-Null
    Copy-Item (Join-Path $Root 'tests\cases\lib\mathlib.ccpl') (Join-Path $pkg 'demo.ccpl')
    Set-Content -Encoding ASCII (Join-Path $pkg 'clm.json') '{ "name": "demo", "version": "1.0.0" }'
    & "$env:WINDIR\System32\tar.exe" -a -c -f (Join-Path $reg 'packages\demo\demo-1.0.0.zip') -C (Join-Path $reg 'pkgroot') demo
    Set-Content -Encoding ASCII (Join-Path $reg 'index.json') '[{"name":"demo","description":"test","latest":"1.0.0","versions":["1.0.0"]}]'
    Copy-Item $clmBuilt (Join-Path $target 'clm.exe')
    $old = $env:CLM_REGISTRY
    $env:CLM_REGISTRY = 'file:///' + ($reg.Replace('\', '/'))
    $clm = Join-Path $target 'clm.exe'
    $install = (& $clm install demo 2>&1 | Out-String)
    $list = (& $clm list 2>&1 | Out-String)
    $bad = (& $clm remove "..\evil" 2>&1 | Out-String)
    $remove = (& $clm remove demo 2>&1 | Out-String)
    $env:CLM_REGISTRY = $old
    if ($install -like '*installed demo 1.0.0*' -and $list -like '*demo*1.0.0*' -and $bad -like '*invalid package name*' -and $remove -like '*removed demo*') {
        Write-Host 'ok clm install list remove'
        $passed++
    } else {
        Write-Host 'FAIL clm smoke test'
        Write-Host $install
        Write-Host $list
        Write-Host $remove
        $failed++
    }
} else {
    Write-Host 'skip clm (build\clm.exe not found)'
}

Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
Write-Host ""
Write-Host "passed: $passed failed: $failed"
if ($failed -gt 0) { exit 1 }