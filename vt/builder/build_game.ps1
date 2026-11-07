# VIRTUA TENNIS - RECOMP : builder
# Generates the recompiled game code from YOUR Virtua Tennis / Virtua Tennis 2 ROMs and compiles it
# into vt_game.dll.
# Genera el código recompilado a partir de TUS ROMs de Virtua Tennis / Virtua Tennis 2 y lo compila
# en vt_game.dll.
#
# Nothing derived from the games is distributed: this runs on your PC with your own files.
# No se distribuye nada derivado de los juegos: todo se hace en tu PC con tus propios archivos.

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$builder = $PSScriptRoot
$root = Split-Path -Parent $builder
$exe = Join-Path $root 'VirtuaTennis-Recompiled.exe'
$toolsDir = Join-Path $builder 'tools'
$tcName = 'llvm-mingw-20251104-ucrt-x86_64'
$tcUrl = "https://github.com/mstorsjo/llvm-mingw/releases/download/20251104/$tcName.zip"
$tcSha256 = '171b298f46fa4b5653dc7b01cc5e709caca645991be8e1b0724466c4a282a1ac'
$genDir = Join-Path $builder 'gen'
$objDir = Join-Path $builder 'obj'
$dataDir = Join-Path $root 'data'

function Say($es, $en) { Write-Host ""; Write-Host "== $es" -ForegroundColor Cyan; Write-Host "   $en" -ForegroundColor DarkCyan }
function Fail($es, $en) { Write-Host ""; Write-Host "ERROR: $es" -ForegroundColor Red; Write-Host "       $en" -ForegroundColor Red; exit 1 }

Say 'VIRTUA TENNIS - RECOMP: construcción del código de los juegos' 'VIRTUA TENNIS - RECOMP: building the game code'

# 1. ROMs (one or both games)
$games = @()
foreach ($id in @('vtennis', 'vtenis2c')) {
	if ((Test-Path (Join-Path $root "roms\$id.zip")) -or (Test-Path (Join-Path $root "$id.zip"))) { $games += $id }
}
if ($games.Count -eq 0) {
	Fail "No se encuentra ningún juego. Copia vtennis.zip y/o vtenis2c.zip en la carpeta 'roms'." "No game found. Copy vtennis.zip and/or vtenis2c.zip to the 'roms' folder."
}
if (-not (Test-Path $exe)) { Fail "No se encuentra $exe" "$exe not found" }
Write-Host "   $($games -join ', ')"

# 2. Compiler (llvm-mingw, free, ~180 MB, only once)
$tcBin = Join-Path $toolsDir "$tcName\bin"
if (-not (Test-Path (Join-Path $tcBin 'x86_64-w64-mingw32-clang++.exe'))) {
	Say "Descargando el compilador llvm-mingw (180 MB, sólo la primera vez)..." "Downloading the llvm-mingw compiler (180 MB, first time only)..."
	New-Item -ItemType Directory -Force -Path $toolsDir | Out-Null
	$zip = Join-Path $toolsDir "$tcName.zip"
	Invoke-WebRequest -Uri $tcUrl -OutFile $zip -UseBasicParsing
	$hash = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLower()
	if ($hash -ne $tcSha256) { Remove-Item $zip; Fail "El archivo descargado no es correcto (SHA-256)." "Downloaded file is corrupt (SHA-256 mismatch)." }
	Say "Descomprimiendo..." "Extracting..."
	$tar = Get-Command tar.exe -ErrorAction SilentlyContinue
	if ($tar) { & tar.exe -xf $zip -C $toolsDir } else { Expand-Archive -Path $zip -DestinationPath $toolsDir -Force }
	Remove-Item $zip
}
$env:PATH = "$tcBin;$env:PATH"

# 3. Generate C++ from each ROM (runs the game up to its entry point and decodes its code)
if (Test-Path $genDir) { Remove-Item -Recurse -Force $genDir }
New-Item -ItemType Directory -Force -Path $genDir | Out-Null
$dumps = @()
foreach ($id in $games) {
	Say "Generando el código C++ a partir de $id.zip..." "Generating C++ code from $id.zip..."
	# relative to the game folder (the executable runs there): avoids problems with non-ASCII paths
	$env:VNBA_AOT = '1'
	$env:VNBA_START_GAME = $id
	$env:VNBA_AOT_DUMP = "builder\gen\dump_$id.cpp"
	$env:VNBA_AOT_STATIC = "builder\seeds_$id.txt"
	$env:VNBA_AOT_STATIC_PTR = '1'
	$env:VNBA_AOT_STATIC_SZ1 = '1'
	$env:VNBA_AOT_STATIC_FUNCS = '1'
	$p = Start-Process -FilePath $exe -WorkingDirectory $root -PassThru -Wait
	Remove-Item Env:VNBA_AOT, Env:VNBA_START_GAME, Env:VNBA_AOT_DUMP, Env:VNBA_AOT_STATIC, Env:VNBA_AOT_STATIC_PTR, Env:VNBA_AOT_STATIC_SZ1, Env:VNBA_AOT_STATIC_FUNCS
	$dump = Join-Path $genDir "dump_$id.cpp"
	if (-not (Test-Path $dump) -or (Get-Item $dump).Length -lt 1000000) {
		Fail "La generación de $id ha fallado (código $($p.ExitCode)). La primera vez hace falta la BIOS naomi.zip en la carpeta 'data'." "Generation of $id failed (code $($p.ExitCode)). The first time, the NAOMI BIOS naomi.zip is needed in the 'data' folder."
	}
	$dumps += "builder\gen\dump_$id.cpp"
}
Say "Preparando las fuentes..." "Preparing the sources..."
$env:VNBA_SPLIT = 'builder\gen'
$env:VNBA_SPLIT_INPUTS = ($dumps -join ';')
$p = Start-Process -FilePath $exe -WorkingDirectory $root -PassThru -Wait
Remove-Item Env:VNBA_SPLIT, Env:VNBA_SPLIT_INPUTS
if (-not (Test-Path (Join-Path $genDir 'aot_gen_table.cpp'))) { Fail "No se han podido preparar las fuentes (código $($p.ExitCode))." "Could not prepare the sources (code $($p.ExitCode))." }
Remove-Item (Join-Path $genDir 'dump_*.cpp') -ErrorAction SilentlyContinue

# 4. Compile
$jobs = [Math]::Max(1, [int]$env:NUMBER_OF_PROCESSORS)
Say "Compilando (puede tardar de 5 a 25 minutos, $jobs núcleos)..." "Compiling (5 to 25 minutes, $jobs cores)..."
if (Test-Path $objDir) { Remove-Item -Recurse -Force $objDir }
New-Item -ItemType Directory -Force -Path $objDir | Out-Null
Push-Location $builder
try {
	& mingw32-make.exe "-j$jobs" --no-print-directory
	if ($LASTEXITCODE -ne 0) { Fail "Error de compilación." "Compilation error." }
} finally { Pop-Location }

# 5. Remember which games are in the module, clean up the generated sources (derived from the games)
New-Item -ItemType Directory -Force -Path $dataDir | Out-Null
Set-Content -Path (Join-Path $dataDir 'vt_games.txt') -Value $games -Encoding ascii
Remove-Item -Recurse -Force $genDir, $objDir
Say "¡Listo! Ya puedes ejecutar VirtuaTennis-Recompiled.exe" "Done! You can now run VirtuaTennis-Recompiled.exe"
