#!/bin/bash

# JSasta VS Code Extension Installation Script

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VSCODE_EXTENSIONS_DIR="$HOME/.vscode/extensions"
EXTENSION_NAME="jsasta-language"

echo "Installing JSasta VS Code Extension..."
echo "--------------------------------------"

# Remove any existing jsasta extensions
echo "Cleaning up old extensions..."
rm -rf "$VSCODE_EXTENSIONS_DIR/jsasta-syntax" 2>/dev/null || true
rm -rf "$VSCODE_EXTENSIONS_DIR/$EXTENSION_NAME" 2>/dev/null || true

# Install npm dependencies
echo "Installing npm dependencies..."
cd "$SCRIPT_DIR"
npm install

# Create symlink
echo "Creating symlink..."
ln -s "$SCRIPT_DIR" "$VSCODE_EXTENSIONS_DIR/$EXTENSION_NAME"

echo ""
echo "✅ Installation complete!"
echo ""
echo "Next steps:"
echo "1. Restart VS Code (or run: Developer: Reload Window)"
echo "2. Open a .jsa file"
echo "3. The extension should activate automatically"
echo ""
echo "To check if the LSP server is working:"
echo "  - Open: View → Output → Select 'JSasta Language Server'"
echo ""
echo "If you see issues, run: jsastad --stdio"
echo "to test the LSP server manually."
