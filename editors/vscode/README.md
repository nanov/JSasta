# JSasta Language Support for VS Code

Full language support for JSasta, including syntax highlighting, diagnostics, hover information, and code completion.

## Features

- **Syntax Highlighting** - Full syntax highlighting for JSasta code
- **Error Diagnostics** - Real-time error and warning detection
- **Hover Information** - See type information by hovering over symbols
- **Code Completion** - Intelligent code completion suggestions
- **Go to Definition** - Jump to symbol definitions
- **Auto-closing Brackets** - Automatic bracket and quote pairing

## Installation

### Prerequisites

You need to have the JSasta LSP server (`jsastad`) installed on your system.

#### Option 1: Install from source

```bash
# Clone the repository
git clone https://github.com/yourusername/JSasta.git
cd JSasta

# Build the compiler and LSP server
make all

# Install to /usr/local/bin
sudo make install
```

#### Option 2: Manual installation

If you build without installing, you can set a custom path in VS Code settings (see Configuration below).

### Installing the Extension

#### Method 1: From source (recommended for development)

```bash
cd editors/vscode

# Install dependencies
npm install

# Link the extension to VS Code
ln -s $(pwd) ~/.vscode/extensions/jsasta-language
```

Then reload VS Code.

#### Method 2: Package and install

```bash
cd editors/vscode
npm install
npx vsce package

# Install the .vsix file
code --install-extension jsasta-language-1.0.0.vsix
```

## Configuration

You can configure the extension in your VS Code settings:

```json
{
    // Custom path to jsastad if not in PATH
    "jsasta.lsp.serverPath": "/path/to/jsastad",
    
    // Enable verbose LSP logging (for debugging)
    "jsasta.lsp.trace.server": "verbose"
}
```

## Usage

Once installed, the extension will automatically activate when you open `.jsa` or `.jsasta` files.

### Features in Action

- **Error Detection**: Syntax and type errors are highlighted as you type
- **Hover**: Hover over variables and functions to see their types
- **Completion**: Press `Ctrl+Space` to trigger code completion
- **Go to Definition**: Right-click a symbol and select "Go to Definition" or press `F12`

## Troubleshooting

### LSP Server Not Found

If you see a warning that `jsastad` is not found:

1. Make sure you've installed JSasta (`make install`)
2. Verify `jsastad` is in your PATH: `which jsastad`
3. Or set a custom path in settings: `jsasta.lsp.serverPath`

### LSP Not Working

1. Check the Output panel (View → Output → Select "JSasta Language Server")
2. Enable verbose logging: Set `jsasta.lsp.trace.server` to `"verbose"`
3. Restart VS Code
4. Check if `jsastad` runs manually: `jsastad --stdio` (should start and wait for input)

### Syntax Highlighting Only

If you only see syntax highlighting but no language features (diagnostics, hover, etc.), the LSP server is not running. See "LSP Server Not Found" above.

## Development

To work on the extension:

```bash
cd editors/vscode
npm install

# Watch for changes (optional)
npm run watch

# Open in VS Code
code .
```

Press `F5` to launch a new VS Code window with the extension loaded.

## Supported File Extensions

- `.jsa` (primary)
- `.jsasta` (alternative)

## Language Features

### Currently Supported

- ✅ Syntax highlighting
- ✅ Real-time diagnostics (errors and warnings)
- ✅ Document parsing and type checking
- ✅ Hover information (basic)
- ✅ Code completion (basic)
- ✅ Go to definition (basic)

### Planned

- ⏳ Find references
- ⏳ Rename symbol
- ⏳ Document symbols
- ⏳ Code formatting
- ⏳ Signature help

## Contributing

Contributions are welcome! See the main repository for contribution guidelines.

## License

MIT

## Links

- [JSasta Compiler Repository](https://github.com/yourusername/JSasta)
- [Report Issues](https://github.com/yourusername/JSasta/issues)
