# Flatpak (Gpredict-Blue)

Application ID: `io.github.jkl213.GpredictBlue`

The `gpredict` binary and `gpredict.desktop` IDs are unchanged so configuration under `~/.config/Gpredict` and Hamlib integration keep working.

## Prerequisites

- `flatpak` and `flatpak-builder`
- Flathub remote for the GNOME runtime:

```bash
flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install -y flathub org.gnome.Platform//47 org.gnome.Sdk//47
```

## Build

From this directory:

```bash
flatpak-builder --force-clean --repo=repo build-dir io.github.jkl213.GpredictBlue.yml
flatpak build-bundle repo gpredict-blue.flatpak io.github.jkl213.GpredictBlue
```

## Local run

```bash
flatpak-builder --run build-dir io.github.jkl213.GpredictBlue.yml gpredict
```

## Install user bundle

```bash
flatpak install --user gpredict-blue.flatpak
flatpak run io.github.jkl213.GpredictBlue
```
