# Bazel Central Registry Publishing Configuration

This directory contains the configuration files for publishing brotli modules to the Bazel Central Registry (BCR).

## Modules

This repository publishes two modules to BCR:

1. **brotli** (root) - The main Brotli C library
2. **brotli_go** (go/) - Go bindings for Brotli

Each module has its own set of template files under `.bcr` and `.bcr/go` respectively.

## Files

### Root module (brotli)
- **metadata.template.json**: Metadata about the brotli module including homepage, maintainers, and repository location
- **source.template.json**: Template for generating source archive URLs for releases
- **presubmit.yml**: BCR CI configuration that defines build and test tasks to validate the module

### Go module (brotli_go)
- **go/metadata.template.json**: Metadata for the brotli_go module
- **go/source.template.json**: Source configuration with path to the go subdirectory
- **go/presubmit.yml**: BCR CI configuration for the Go module

### Shared
- **config.yml**: Configuration specifying both modules via moduleRoots
- **README.md**: This file

## Setup (One-time)

Before you can publish to BCR, you need to set up the following:

1. **Fork the Bazel Central Registry**
   - Fork https://github.com/bazelbuild/bazel-central-registry to your GitHub account

2. **Create a Personal Access Token**
   - Go to GitHub Settings > Developer settings > Personal access tokens > Tokens (classic)
   - Click "Generate new token (classic)"
   - Select scopes: `repo` and `workflow`
   - Generate the token and copy it

3. **Add the token to repository secrets**
   - Go to the brotli repository Settings > Secrets and variables > Actions
   - Click "New repository secret"
   - Name: `BCR_PUBLISH_TOKEN`
   - Value: Paste the token from step 2
   - Click "Add secret"

## Publishing Process

To publish new versions of both modules to BCR:

1. Create a GitHub release with a tag (e.g., `v1.2.1`)
2. Go to the Actions tab in GitHub
3. Select the "Publish to BCR" workflow
4. Click "Run workflow"
5. Enter the release tag name (e.g., `v1.2.1`)
6. Enter your BCR fork (e.g., `yourusername/bazel-central-registry`)
7. Click "Run workflow"

The workflow will automatically create pull requests to the Bazel Central Registry for both modules.

## References

- [Bazel Central Registry](https://github.com/bazelbuild/bazel-central-registry)
- [publish-to-bcr Documentation](https://github.com/bazel-contrib/publish-to-bcr)
- [Bzlmod User Guide](https://bazel.build/external/module)
- [Multi-module Publishing](https://github.com/bazel-contrib/publish-to-bcr/tree/main/templates#optional-configyml)
