#!/bin/bash

# JSasta Editor Support Installation Script

set -e

echo "JSasta Editor Support Installer"
echo "================================"
echo ""

# Detect OS
OS="$(uname -s)"
case "${OS}" in
    Linux*)     MACHINE=Linux;;
    Darwin*)    MACHINE=Mac;;
    CYGWIN*|MINGW*|MSYS*) MACHINE=Windows;;
    *)          MACHINE="UNKNOWN:${OS}"
esac

echo "Detected OS: $MACHINE"
echo ""

# Function to install for VS Code
install_vscode() {
    echo "Installing JSasta syntax for Visual Studio Code..."

    if [ "$MACHINE" = "Mac" ]; then
        VSCODE_EXT="$HOME/.vscode/extensions"
    elif [ "$MACHINE" = "Linux" ]; then
        VSCODE_EXT="$HOME/.vscode/extensions"
    elif [ "$MACHINE" = "Windows" ]; then
        VSCODE_EXT="$USERPROFILE/.vscode/extensions"
    fi

    mkdir -p "$VSCODE_EXT/jsasta-syntax"
    cp -r vscode/* "$VSCODE_EXT/jsasta-syntax/"
    echo "✓ VS Code extension installed to: $VSCODE_EXT/jsasta-syntax"
    echo "  Please reload VS Code to activate."
}

# Function to install for Neovim
install_neovim() {
    echo "Installing JSasta syntax for Neovim..."

    mkdir -p "$HOME/.config/nvim/syntax"
    cp vim/jsasta.vim "$HOME/.config/nvim/syntax/"

    if [ ! -f "$HOME/.config/nvim/init.vim" ]; then
        touch "$HOME/.config/nvim/init.vim"
    fi

    if ! grep -q "filetype=jsasta" "$HOME/.config/nvim/init.vim" 2>/dev/null; then
        echo "au BufRead,BufNewFile *.js,*.jsasta set filetype=jsasta" >> "$HOME/.config/nvim/init.vim"
    fi

    echo "✓ Neovim syntax installed to: $HOME/.config/nvim/syntax/jsasta.vim"
    echo "  Added filetype detection to ~/.config/nvim/init.vim"
}

# Main menu
echo "Select editor to install syntax support:"
echo "1) Visual Studio Code"
echo "2) Vim"
echo "3) Neovim"
echo "4) Exit"
echo ""
read -p "Enter choice [1-4]: " choice

case $choice in
    1)
        install_vscode
        ;;
    2)
        install_vim
        ;;
    3)
        install_neovim
        ;;
    4)
        echo "Exiting."
        exit 0
        ;;
    *)
        echo "Invalid choice. Exiting."
        exit 1
        ;;
esac

echo ""
echo "================================"
echo "Installation complete!"
echo ""
echo "For Sublime Text, please install manually:"
echo "  See README.md for instructions"
