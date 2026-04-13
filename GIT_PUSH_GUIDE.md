# Git Push Configuration Guide

## Problem
You're seeing the error:
```
remote: Permission to cubasnik/vEPC.git denied to WinterWind3.
fatal: unable to access 'https://github.com/cubasnik/vEPC.git/': The requested URL returned error: 403
```

This typically means:
1. Your GitHub account doesn't have push access to the repository
2. HTTPS authentication is not properly configured
3. The remote URL needs to be changed

## Solution Options

### Option 1: Use SSH Key (Recommended)

#### Generate SSH key (if you don't have one)
```bash
ssh-keygen -t rsa -b 4096 -f ~/.ssh/id_rsa -N ""
```

#### Add SSH key to GitHub
1. Copy the public key: `cat ~/.ssh/id_rsa.pub`
2. Go to https://github.com/settings/keys
3. Click "New SSH key"
4. Paste your public key

#### Update remote URL to use SSH
```bash
cd c:\Users\Winter\Documents\Работа\vEPC
git remote set-url origin git@github.com:cubasnik/vEPC.git
```

#### Try pushing again
```bash
git push origin main
```

### Option 2: Use Personal Access Token (HTTPS)

#### Create a personal access token
1. Go to https://github.com/settings/tokens
2. Click "Generate new token"
3. Select scopes: `repo` (all), `admin:repo_hook`
4. Copy the token (save it securely!)

#### Configure Git to use the token
```bash
# When prompted for password, use your token instead:
git push origin main

# Or cache credentials:
# Windows
git config --global credential.helper wincred

# Linux/Mac
git config --global credential.helper osxkeychain
```

### Option 3: Verify Repository Access

If you're not the repository owner:

```bash
# Check current remote
git remote -v

# Check if you have forked the repo or have collaborator access
# Visit: https://github.com/cubasnik/vEPC
```

If you need write access:
- Contact the repository owner (cubasnik)
- Or fork the repository and submit a pull request

## Verify Configuration

### Check current remote
```bash
git remote -v
```

Should show:
```
origin  git@github.com:cubasnik/vEPC.git (fetch)
origin  git@github.com:cubasnik/vEPC.git (push)
```

Or for HTTPS with token:
```
origin  https://github.com/cubasnik/vEPC.git (fetch)
origin  https://github.com/cubasnik/vEPC.git (push)
```

### Test connection
```bash
# For SSH
ssh -T git@github.com

# For HTTPS
git ls-remote https://github.com/cubasnik/vEPC.git
```

## Push Your Changes

Once configured:

```bash
cd c:\Users\Winter\Documents\Работа\vEPC
git push origin main
```

You should see:
```
Enumerating objects: 14, done.
Counting objects: 100% (14/14), done.
...
remote: Create a pull request for 'main'
```

## Commit Messages

Your pending commits to push:

```
8a4ec3a - docs: Add comprehensive README and Docker ignore file
1c8a06e - feat(cli): Integrate Cisco-style commands and Linux interface management  
309f1ca - feat(docker, ansible, cli): Add containerization, deployment automation, and Cisco-style CLI support
```

## Additional Help

- GitHub SSH Setup: https://docs.github.com/en/authentication/connecting-to-github-with-ssh
- Personal Access Tokens: https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/creating-a-personal-access-token
- GitHub CLI: https://cli.github.com/

## Troubleshooting

If you still can't push:
```bash
# Check git config
git config --list

# Check SSH agent (for SSH keys)
ssh-add -l

# Remove cached credentials (if using HTTPS)
# Windows
credential remove https://github.com

# Linux/Mac
git credential-osxkeychain erase host=github.com
```

---

**Once you've pushed successfully**, the changes will be visible on GitHub!
