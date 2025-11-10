# JSasta Language Support for VS Code

Full language support for JSasta, including syntax highlighting, diagnostics, hover information, and code completion powered by the JSasta Language Server Protocol (LSP).

## Features

- **Syntax Highlighting** - Full syntax highlighting for JSasta code including:
  - Keywords: `if`, `else`, `while`, `for`, `return`, `break`, `continue`
  - Type declarations: `struct`, `enum`, `trait`, `impl`, `function`
  - Type operators: `is`, `as`, `new`, `delete`, `let`
  - Primitive types: `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`, `int`, `double`, `string`, `bool`, `void`
  - Modifiers: `const`, `var`, `let`, `external`, `ref`
  - Import/export statements with builtin module support (`@io`, `@test`, etc.)
  
- **Error Diagnostics** - Real-time error and warning detection via LSP
- **Hover Information** - See type information by hovering over symbols
- **Code Completion** - Intelligent code completion suggestions
- **Go to Definition** - Jump to symbol definitions
- **Auto-closing Brackets** - Automatic bracket and quote pairing

## Installation

### Prerequisites

You need to have both the JSasta compiler (`jsastac`) and LSP server (`jsastad`) built on your system.

#### Build from source

```bash
# Clone the repository
git clone https://github.com/yourusername/JSasta.git
cd JSasta

# Build the compiler, LSP server, and test runner
make all

# The binaries will be in: build/release/
# - jsastac  (compiler)
# - jsastad  (LSP server)
# - jsastat  (test runner)
```

#### Optional: Install to system PATH

```bash
# Install to /usr/local/bin (requires sudo)
sudo make install

# Or add to your PATH manually
export PATH="$PATH:/path/to/JSasta/build/release"
```

### Installing the VSCode Extension

#### Method 1: Symlink Installation (Recommended for Development)

This method creates a symlink from your VSCode extensions directory to the extension source. Changes to the extension will be reflected immediately.

```bash
cd editors/vscode

# Run the installation script
./install.sh
```

Or manually:

```bash
cd editors/vscode

# Install npm dependencies
npm install

# Create symlink
ln -s "$(pwd)" ~/.vscode/extensions/jsasta-language

# Reload VSCode (Command Palette → Developer: Reload Window)
```

#### Method 2: Package and Install (Production)

```bash
cd editors/vscode

# Install dependencies
npm install

# Package the extension
npx vsce package

# Install the .vsix file
code --install-extension jsasta-language-1.0.0.vsix
```

## Configuration

The extension automatically looks for `jsastad` in your PATH. If you haven't installed it system-wide, you can configure a custom path:

```json
{
    // Custom path to jsastad LSP server (if not in PATH)
    "jsasta.lsp.serverPath": "/path/to/JSasta/build/release/jsastad",
    
    // Enable verbose LSP logging (useful for debugging)
    "jsasta.lsp.trace.server": "verbose"
}
```

### Example: Using Local Build

If you haven't run `make install`, point to your local build:

```json
{
    "jsasta.lsp.serverPath": "/Users/yourname/JSasta/build/release/jsastad"
}
```

## Usage

Once installed, the extension will automatically activate when you open `.jsa` or `.jsasta` files.

### Features in Action

- **Error Detection**: Syntax and type errors are highlighted as you type
- **Hover**: Hover over variables and functions to see their types
- **Completion**: Press `Ctrl+Space` (or `Cmd+Space` on Mac) to trigger code completion
- **Go to Definition**: Right-click a symbol and select "Go to Definition" or press `F12`

### Example Code

```jsasta
// Enums with pattern matching
enum Result {
    Ok { value: i32 };
    Err { message: string };
}

// Struct with methods
struct Point {
    x: i32;
    y: i32;
    
    distance(self): i32 {
        return self.x * self.x + self.y * self.y;
    }
}

// Imports from builtin modules
import io from @io;
import t from @test;

function main(): void {
    let p = Point{ x: 3, y: 4 };
    io.println("Distance: {}", p.distance());
    
    let result = Result.Ok{ value: 42 };
    if (result is Result.Ok(let ok)) {
        t.assert.equals(42, ok.value);
    }
}
```

## Troubleshooting

### LSP Server Not Found

If you see a warning that `jsastad` is not found:

1. **Verify the LSP server is built:**
   ```bash
   ls build/release/jsastad
   ```

2. **Check if it's in your PATH:**
   ```bash
   which jsastad
   # or test it directly
   ./build/release/jsastad --stdio
   ```

3. **Set custom path in VSCode settings:**
   - Open Settings (`Cmd+,` or `Ctrl+,`)
   - Search for "jsasta"
   - Set `jsasta.lsp.serverPath` to your `jsastad` binary path

### LSP Not Working

1. **Check the Output panel:**
   - View → Output
   - Select "JSasta Language Server" from the dropdown
   
2. **Enable verbose logging:**
   - Set `jsasta.lsp.trace.server` to `"verbose"` in settings
   - Restart VSCode
   
3. **Test LSP server manually:**
   ```bash
   jsastad --stdio
   # Should start and wait for input (press Ctrl+C to exit)
   ```

4. **Rebuild the LSP server:**
   ```bash
   make clean && make all
   ```

### Syntax Highlighting Only

If you only see syntax highlighting but no language features (diagnostics, hover, etc.), the LSP server is not running. See "LSP Server Not Found" above.

### Extension Not Activating

1. **Check the extension is enabled:**
   - Extensions view → Search for "JSasta"
   - Make sure it's enabled

2. **Check file association:**
   - Files must have `.jsa` or `.jsasta` extension
   
3. **Reload window:**
   - Command Palette (`Cmd+Shift+P` or `Ctrl+Shift+P`)
   - Type "Reload Window"

## Development

To work on the extension:

```bash
cd editors/vscode

# Install dependencies
npm install

# Open in VSCode
code .

# Press F5 to launch a new VSCode window with the extension loaded
```

### Making Changes

- **Syntax highlighting:** Edit `jsasta.tmLanguage.json`
- **LSP client:** Edit `extension.js`
- **Configuration:** Edit `package.json`
- **Language config:** Edit `language-configuration.json`

After making changes:
- For syntax changes: Reload the window (`Cmd+R` or `Ctrl+R`)
- For extension code: Stop debugging and press `F5` again

## Supported File Extensions

- `.jsa` (primary)
- `.jsasta` (alternative)

## Language Features Status

### Currently Supported

- ✅ Syntax highlighting (all keywords, types, operators)
- ✅ Real-time diagnostics (errors and warnings)
- ✅ Document parsing and type checking
- ✅ Hover information
- ✅ Code completion
- ✅ Go to definition
- ✅ Enum and pattern matching support
- ✅ Trait and impl syntax
- ✅ Builtin module support (`@io`, `@test`, etc.)

### Planned

- ⏳ Find references
- ⏳ Rename symbol
- ⏳ Document symbols (outline view)
- ⏳ Code formatting
- ⏳ Signature help (parameter hints)
- ⏳ Inlay hints (type hints)

## Uninstalling

### Symlink Installation

```bash
rm ~/.vscode/extensions/jsasta-language
```

### Package Installation

```bash
code --uninstall-extension jsasta-language
```

## Contributing

Contributions are welcome! To contribute:

1. Fork the repository
2. Make your changes
3. Test the extension
4. Submit a pull request

## License

MIT

## Links

- [JSasta Compiler Repository](https://github.com/yourusername/JSasta)
- [Report Issues](https://github.com/yourusername/JSasta/issues)
- [Language Documentation](https://github.com/yourusername/JSasta/docs)
