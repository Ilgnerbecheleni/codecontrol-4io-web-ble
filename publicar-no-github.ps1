# Execute no PowerShell no diretorio principal descompactado.
# Requer Git e GitHub CLI (gh). Autenticacao pelo browser com: gh auth login
$ErrorActionPreference = 'Stop'
$repoName = 'codecontrol-4io-web-ble'
Write-Host "Publicacao do $repoName" -ForegroundColor Cyan
if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw 'Git nao encontrado. Instale Git for Windows.' }
if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { throw 'GitHub CLI nao encontrado. Instale a ferramenta gh.' }
gh auth status
if ($LASTEXITCODE -ne 0) { throw 'Autentique com: gh auth login' }
if (-not (Test-Path .git)) {
  git init
  git branch -M main
}
git add .
git commit -m 'feat: configurador BLE ESP32 4IO'
if ($LASTEXITCODE -ne 0) { Write-Host 'Sem alteracoes novas para commit.' }
$owner = (gh api user --jq '.login').Trim()
$existing = gh repo view "$owner/$repoName" --json name 2>$null
if ($LASTEXITCODE -ne 0) {
  gh repo create $repoName --public --source . --remote origin --push --description 'Configurador Web Bluetooth e firmware ESP32 4IO'
} else {
  if (-not (git remote | Select-String '^origin$')) { git remote add origin "https://github.com/$owner/$repoName.git" }
  git push -u origin main
}
Write-Host "Repositorio: https://github.com/$owner/$repoName" -ForegroundColor Green
Write-Host "Agora habilite Pages > Source: GitHub Actions em Settings > Pages." -ForegroundColor Yellow
