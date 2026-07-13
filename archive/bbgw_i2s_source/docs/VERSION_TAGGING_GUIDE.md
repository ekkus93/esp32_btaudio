# Version Tagging and GitHub Release Guide

## Overview

This guide documents the process for creating version tags and GitHub releases for the BeagleBone Green Wireless I2S Source project.

---

## Version Numbering Scheme

### Format: `v<MAJOR>.<MINOR>.<PATCH>-<PLATFORM>`

- **MAJOR**: Breaking changes or major feature additions
- **MINOR**: New features, backward-compatible
- **PATCH**: Bug fixes, documentation updates
- **PLATFORM**: Target platform identifier

### Examples
- `v1.0.0-bbgw` — First stable release for BeagleBone Green Wireless
- `v1.1.0-bbgw` — New features (e.g., PRU integration)
- `v1.0.1-bbgw` — Bug fixes or documentation updates

### Platform Identifiers
- `bbgw` — BeagleBone Green Wireless
- `rpi` — Raspberry Pi (original platform)
- `bbai` — BeagleBone AI (future)

---

## Creating a Version Tag

### 1. Verify Working Directory Clean

```bash
git status
# On branch master
# Your branch is up to date with 'origin/master'.
# nothing to commit, working tree clean
```

### 2. Create Annotated Tag

```bash
git tag -a v1.0.0-bbgw -m "Release v1.0.0-bbgw

BeagleBone Green Wireless I2S Source - Initial Release

Features:
- I2S audio output via McASP (48 kHz stereo)
- UART4 control interface for ESP32 Bluetooth
- Web UI with Server-Sent Events
- Comprehensive documentation (9 guides, 8,925+ lines)
- Automated setup script (10 steps)
- Complete test suite (unit, integration, performance)

See RELEASE_NOTES.md for full details."
```

**Tag naming rules:**
- Use annotated tags (`-a`), not lightweight tags
- Include descriptive message (`-m`)
- Reference RELEASE_NOTES.md for full details

### 3. Verify Tag Created

```bash
git tag -l "v1.0.0-bbgw"
# v1.0.0-bbgw

git show v1.0.0-bbgw
# Shows tag message and commit details
```

### 4. Push Tag to GitHub

```bash
git push origin v1.0.0-bbgw
# Enumerating objects: ...
# Counting objects: ...
# Writing objects: ...
# Total ... (delta ...)
# To github.com:ekkus93/esp32_btaudio.git
#  * [new tag]         v1.0.0-bbgw -> v1.0.0-bbgw
```

**Note**: Tags are not pushed by `git push` by default — must explicitly push tags.

---

## Creating a GitHub Release

### Option 1: GitHub Web Interface (Recommended)

1. **Navigate to Repository**:
   - Go to https://github.com/ekkus93/esp32_btaudio

2. **Click "Releases"**:
   - In right sidebar, click "Releases" (or navigate to /releases)

3. **Draft a New Release**:
   - Click "Draft a new release"

4. **Configure Release**:
   - **Tag**: Select `v1.0.0-bbgw` from dropdown
   - **Release Title**: `v1.0.0-bbgw — BeagleBone Green Wireless I2S Source (Initial Release)`
   - **Description**: Copy/paste from RELEASE_NOTES.md (Overview, Features, Quick Start)

5. **Attach Files** (Optional):
   - **Setup Script**: `setup_bbgw.sh`
   - **Compiled Device Tree Overlays**: `AM335X-UART4.dtbo`, `AM335X-I2S.dtbo` (if available)
   - **Documentation Archive**: `bbgw_i2s_source_docs_v1.0.0.tar.gz` (optional)

6. **Publish Release**:
   - Click "Publish release"

---

### Option 2: GitHub CLI (gh)

**Prerequisites**: Install GitHub CLI
```bash
# Debian/Ubuntu
curl -fsSL https://cli.github.com/packages/githubcli-archive-keyring.gpg | sudo dd of=/usr/share/keyrings/githubcli-archive-keyring.gpg
echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/githubcli-archive-keyring.gpg] https://cli.github.com/packages stable main" | sudo tee /etc/apt/sources.list.d/github-cli.list > /dev/null
sudo apt update
sudo apt install gh
```

**Authenticate**:
```bash
gh auth login
# Follow prompts to authenticate with GitHub
```

**Create Release**:
```bash
gh release create v1.0.0-bbgw \
  --title "v1.0.0-bbgw — BeagleBone Green Wireless I2S Source (Initial Release)" \
  --notes-file RELEASE_NOTES.md \
  setup_bbgw.sh \
  docs/AM335X-UART4.dtbo \
  docs/AM335X-I2S.dtbo
```

**Options**:
- `--title`: Release title
- `--notes-file`: Path to release notes (or use `--notes "..."` for inline)
- Remaining arguments: Files to attach

---

## Verifying Release

### Check GitHub Repository
1. Navigate to https://github.com/ekkus93/esp32_btaudio/releases
2. Verify `v1.0.0-bbgw` appears in release list
3. Verify attached files (if any)
4. Verify release notes rendered correctly

### Check Tag Locally
```bash
git fetch --tags
git tag -l "v1.0.0-bbgw"
# v1.0.0-bbgw

git show v1.0.0-bbgw
# Shows tag annotation and commit
```

### Clone Repository (Fresh Test)
```bash
cd /tmp
git clone --depth 1 --branch v1.0.0-bbgw https://github.com/ekkus93/esp32_btaudio.git
cd esp32_btaudio/bbgw_i2s_source
bash setup_bbgw.sh
# Verify setup works from tagged release
```

---

## Creating Documentation Archive (Optional)

For releases, you may want to create a standalone documentation archive:

```bash
cd /home/phil/work/esp32/esp32_btaudio/bbgw_i2s_source

# Create tarball of docs
tar -czf bbgw_i2s_source_docs_v1.0.0.tar.gz \
  docs/*.md \
  RELEASE_NOTES.md \
  README.md \
  setup_bbgw.sh

# Verify contents
tar -tzf bbgw_i2s_source_docs_v1.0.0.tar.gz
```

Upload this archive to the GitHub release for offline documentation access.

---

## Device Tree Overlay Packaging

### Option 1: Source-Only (Current)

**Recommended for v1.0.0-bbgw**: Include Device Tree source (`.dts`) files in repository, provide compilation instructions in `BBGW_DEVICE_TREE_GUIDE.md`.

**Rationale**:
- Users compile overlays for their specific kernel version
- No compatibility issues with different kernel versions
- Avoids pre-compiled binary distribution issues

**Files in Repository**:
- `docs/AM335X-UART4.dts` (UART4 overlay source)
- `docs/AM335X-I2S.dts` (McASP I2S overlay source - if created)
- `docs/BBGW_DEVICE_TREE_GUIDE.md` (compilation instructions)

**GitHub Release Attachments**: None (users compile from source)

---

### Option 2: Pre-Compiled Binaries (Future)

**Use case**: Convenience for specific kernel versions

**Process**:
```bash
# Compile overlays
dtc -@ -I dts -O dtb -o AM335X-UART4.dtbo docs/AM335X-UART4.dts
dtc -@ -I dts -O dtb -o AM335X-I2S.dtbo docs/AM335X-I2S.dts

# Attach to GitHub release
gh release upload v1.0.0-bbgw \
  AM335X-UART4.dtbo \
  AM335X-I2S.dtbo \
  --clobber
```

**Warning in release notes**:
```markdown
## Pre-Compiled Device Tree Overlays

**Kernel Version**: 5.10.168-ti-r72  
**Note**: Overlays compiled for specific kernel. If you have a different kernel version, compile from source (see `docs/BBGW_DEVICE_TREE_GUIDE.md`).
```

---

## Release Checklist

Before creating a release, verify:

- [ ] All tests passing (`pytest tests/ -v`)
- [ ] Documentation complete and accurate
- [ ] `RELEASE_NOTES.md` created with full feature list
- [ ] `config.yaml.template` up to date
- [ ] `requirements.txt` up to date
- [ ] `setup_bbgw.sh` tested on clean system
- [ ] Git working directory clean (`git status`)
- [ ] Latest changes committed and pushed
- [ ] Version tag created locally (`git tag -a v1.0.0-bbgw`)
- [ ] Version tag pushed to GitHub (`git push origin v1.0.0-bbgw`)
- [ ] GitHub release created (web or CLI)
- [ ] Release notes copied to GitHub release
- [ ] Files attached (setup script, docs archive)
- [ ] Release verified (clone, setup, run)

---

## Hotfix Release Process

For critical bug fixes after release:

### 1. Create Hotfix Branch
```bash
git checkout -b hotfix/v1.0.1-bbgw v1.0.0-bbgw
```

### 2. Apply Fix
```bash
# Edit files
git add <files>
git commit -m "fix: <description>"
```

### 3. Test Thoroughly
```bash
pytest tests/ -v
```

### 4. Merge to Master
```bash
git checkout master
git merge --no-ff hotfix/v1.0.1-bbgw
```

### 5. Tag and Release
```bash
git tag -a v1.0.1-bbgw -m "Hotfix release v1.0.1-bbgw

Bug Fixes:
- <issue 1>
- <issue 2>
"
git push origin master
git push origin v1.0.1-bbgw
```

### 6. Update Release Notes
```bash
# Edit RELEASE_NOTES.md — add v1.0.1-bbgw section
git add RELEASE_NOTES.md
git commit -m "docs: Update RELEASE_NOTES.md for v1.0.1-bbgw"
git push origin master
```

### 7. Create GitHub Release
```bash
gh release create v1.0.1-bbgw \
  --title "v1.0.1-bbgw — Bug Fixes" \
  --notes "See RELEASE_NOTES.md for details."
```

---

## Rollback Procedure

If a release has critical issues:

### 1. Delete GitHub Release
```bash
gh release delete v1.0.0-bbgw --yes
```

### 2. Delete Remote Tag
```bash
git push --delete origin v1.0.0-bbgw
```

### 3. Delete Local Tag
```bash
git tag -d v1.0.0-bbgw
```

### 4. Fix Issues
```bash
# Apply fixes
git add <files>
git commit -m "fix: <description>"
git push origin master
```

### 5. Re-Tag and Re-Release
```bash
# Recreate tag with same version
git tag -a v1.0.0-bbgw -m "Release v1.0.0-bbgw (re-released after fix)"
git push origin v1.0.0-bbgw

# Recreate GitHub release
gh release create v1.0.0-bbgw --notes-file RELEASE_NOTES.md
```

**Note**: Only rollback if release is <24 hours old and has minimal downloads. For older releases, create a new patch version instead (v1.0.1-bbgw).

---

## Best Practices

### Tag Messages
- **Be descriptive**: Explain what's in the release
- **Reference docs**: Point to RELEASE_NOTES.md for full details
- **List key features**: 3-5 bullet points of major changes

### Release Notes
- **Start with overview**: What is this project?
- **Feature list**: Organized by category
- **Known issues**: Document workarounds
- **Migration guide**: If upgrading from previous version
- **Quick start**: Get users running quickly
- **Support info**: Where to get help

### File Attachments
- **Setup script**: Always include automated setup
- **Documentation**: Archive of all docs (optional)
- **Binaries**: Only if needed (e.g., pre-compiled overlays)
- **Source tarball**: GitHub auto-generates

### Versioning
- **Semantic Versioning**: Major.Minor.Patch
- **Platform suffix**: -bbgw, -rpi, etc.
- **Pre-releases**: Use `-alpha`, `-beta`, `-rc1` (e.g., v1.1.0-bbgw-beta.1)

---

## Example: v1.0.0-bbgw Release Commands

**Full release workflow:**

```bash
# 1. Verify working directory clean
git status

# 2. Create annotated tag
git tag -a v1.0.0-bbgw -m "Release v1.0.0-bbgw

BeagleBone Green Wireless I2S Source - Initial Release

Features:
- I2S audio output via McASP (48 kHz stereo)
- UART4 control interface
- Web UI with SSE
- Comprehensive documentation (9 guides)
- Automated setup script
- Complete test suite

See RELEASE_NOTES.md for full details."

# 3. Push tag to GitHub
git push origin v1.0.0-bbgw

# 4. Create GitHub release (web interface or CLI)
gh release create v1.0.0-bbgw \
  --title "v1.0.0-bbgw — BeagleBone Green Wireless I2S Source (Initial Release)" \
  --notes-file RELEASE_NOTES.md \
  setup_bbgw.sh

# 5. Verify release
gh release view v1.0.0-bbgw

# 6. Test clone from tag
cd /tmp
git clone --depth 1 --branch v1.0.0-bbgw https://github.com/ekkus93/esp32_btaudio.git
cd esp32_btaudio/bbgw_i2s_source
bash setup_bbgw.sh
```

---

## Summary

**Key Commands:**
- `git tag -a <version> -m "<message>"` — Create annotated tag
- `git push origin <version>` — Push tag to GitHub
- `gh release create <version> --notes-file RELEASE_NOTES.md` — Create GitHub release
- `git clone --branch <version>` — Clone specific release

**Key Files:**
- `RELEASE_NOTES.md` — Comprehensive release documentation
- `setup_bbgw.sh` — Automated setup script (attach to release)
- `docs/*.md` — Documentation (optionally package as tarball)

**Key Principles:**
- Use semantic versioning with platform suffix
- Create annotated tags with descriptive messages
- Write comprehensive release notes
- Test release thoroughly before publishing
- Provide rollback procedure for critical issues

---

**Version**: 1.0.0-bbgw  
**Last Updated**: 2026-02-07  
**Status**: Complete
