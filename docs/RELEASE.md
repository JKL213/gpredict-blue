# Releases (GitHub)

Automated uploads run on **version tags** matching `v*` (for example `v2.3.54-blue1`).

## What CI does

Workflow: `.github/workflows/release.yml`

1. `./autogen.sh`, `make`, `make check` (includes `test-layout-code`)
2. `make dist` → attaches `gpredict-*.tar.bz2` to the GitHub Release
3. Builds `gpredict-blue.flatpak` and attaches it to the same release

Required: repository setting **Actions → General → Workflow permissions → Read and write permissions** (so `GITHUB_TOKEN` can create releases).

## Manual release (if CI is unavailable)

```bash
git tag vX.Y.Z
git push origin vX.Y.Z
```

Or create the tag in GitHub UI; the workflow runs automatically.

### Manual asset upload

```bash
./autogen.sh && make check && make dist
gh release create vX.Y.Z gpredict-*.tar.bz2 --generate-notes
# Optional Flatpak:
cd flatpak
flatpak-builder --force-clean --user --repo=repo build-dir io.github.jkl213.GpredictBlue.yml
flatpak build-bundle repo gpredict-blue.flatpak io.github.jkl213.GpredictBlue
gh release upload vX.Y.Z gpredict-blue.flatpak
```
