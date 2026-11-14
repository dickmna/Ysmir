param(
    [Parameter(Mandatory=$true)][string]$GameData,
    [Parameter(Mandatory=$true)][string]$ForcefulTongue,
    [Parameter(Mandatory=$true)][string]$ElementalFury,
    [Parameter(Mandatory=$true)][string]$WhirlwindSprint,
    [Parameter(Mandatory=$true)][string]$PapyrusCompiler,
    [Parameter(Mandatory=$true)][string[]]$PapyrusImports,
    [Parameter(Mandatory=$true)][string]$PapyrusFlags,
    [Parameter(Mandatory=$true)][string]$Champollion
)
$ErrorActionPreference = 'Stop'
$taskRoot = $PSScriptRoot
$env:YSMIR_GAME_DATA = (Resolve-Path -LiteralPath $GameData).Path
$env:YSMIR_FT_PLUGIN = (Resolve-Path -LiteralPath $ForcefulTongue).Path
$env:YSMIR_ELEMENTAL_PLUGIN = (Resolve-Path -LiteralPath $ElementalFury).Path
$env:YSMIR_SPRINT_PLUGIN = (Resolve-Path -LiteralPath $WhirlwindSprint).Path
$taskProject = Join-Path $taskRoot 'builder\Ysmir.Builder.csproj'
& dotnet build $taskProject --nologo
if ($LASTEXITCODE -ne 0) { throw 'Builder compilation failed.' }
& dotnet exec (Join-Path $taskRoot 'builder\bin\Debug\net9.0\Ysmir.Builder.dll') $taskRoot
if ($LASTEXITCODE -ne 0) { throw 'Plugin generation failed.' }
$taskSources = Join-Path $taskRoot 'package\Source\Scripts'
$taskPex = Join-Path $taskRoot 'package\Scripts'
$taskDecompiled = Join-Path $taskRoot 'verify-decompiled'
New-Item -ItemType Directory -Path $taskPex,$taskDecompiled -Force | Out-Null
$taskImports = (@($taskSources) + @($PapyrusImports | ForEach-Object { (Resolve-Path -LiteralPath $_).Path })) -join ';'
foreach ($taskSource in Get-ChildItem -LiteralPath $taskSources -Filter '*.psc') {
    & $PapyrusCompiler $taskSource.BaseName "-i=$taskImports" "-o=$taskPex" "-f=$PapyrusFlags"
    if ($LASTEXITCODE -ne 0) { throw "Script compilation failed: $($taskSource.Name)" }
}
foreach ($taskScript in Get-ChildItem -LiteralPath $taskPex -Filter '*.pex') {
    & $Champollion $taskScript.FullName -p $taskDecompiled --no-debug-line
    if ($LASTEXITCODE -ne 0) { throw "Script verification failed: $($taskScript.Name)" }
}
& python (Join-Path $taskRoot 'verify.py')
if ($LASTEXITCODE -ne 0) { throw 'Offline validation failed.' }
