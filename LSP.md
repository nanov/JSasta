# JSasta Language Server Protocol (LSP)

JSasta includes a fully-featured Language Server Protocol implementation (`jsastad`) that provides rich IDE features for any editor that supports LSP.

## Features

### Currently Implemented ✅
- **Real-time Diagnostics** - Parse and type errors as you type
- **Document Synchronization** - Full document sync with incremental updates
- **Hover Information** - Basic type information on hover (work in progress)
- **Code Completion** - Context-aware code suggestions (work in progress)
- **Go to Definition** - Jump to symbol definitions (work in progress)

### Planned 🚧
- Find all references
- Rename symbol
- Document symbols/outline
- Signature help
- Code formatting
- Semantic highlighting

## Installation

### Building the LSP Server

The LSP server is built automatically when you build JSasta:

```bash
cd JSasta
make all          # Build both compiler and LSP server
sudo make install # Install to /usr/local/bin
```

This creates two binaries:
- `jsastac` - The compiler
- `jsastad` - The LSP daemon

### Verify Installation

```bash
which jsastad
# Should output: /usr/local/bin/jsastad

jsastad --help
# Should show usage information
```

## Editor Setup

### Visual Studio Code

#### Install Extension

```bash
cd editors/vscode
npm install
ln -s $(pwd) ~/.vscode/extensions/jsasta-language
```

Reload VS Code, and the extension will automatically connect to `jsastad`.

#### Configuration

Add to your `settings.json`:

```json
{
  "jsasta.lsp.serverPath": "/usr/local/bin/jsastad",
  "jsasta.lsp.trace.server": "off"  // or "messages" or "verbose" for debugging
}
```

### Neovim

#### Prerequisites

You need `nvim-lspconfig` and optionally `nvim-cmp` for completions:

```lua
-- Using packer.nvim
use 'neovim/nvim-lspconfig'
use 'hrsh7th/nvim-cmp'
use 'hrsh7th/cmp-nvim-lsp'
```

#### Configuration

Add to your config (or source the provided config file):

```lua
-- ~/.config/nvim/init.lua or ~/.config/nvim/lua/lsp/jsasta.lua

local lspconfig = require('lspconfig')
local configs = require('lspconfig.configs')

-- Define JSasta LSP
if not configs.jsasta then
  configs.jsasta = {
    default_config = {
      cmd = { 'jsastad', '--stdio' },
      filetypes = { 'jsasta' },
      root_dir = lspconfig.util.root_pattern('.git'),
      settings = {},
    },
  }
end

-- Setup
lspconfig.jsasta.setup({
  on_attach = function(client, bufnr)
    -- Your keybindings here
    local bufopts = { noremap=true, silent=true, buffer=bufnr }
    vim.keymap.set('n', 'gd', vim.lsp.buf.definition, bufopts)
    vim.keymap.set('n', 'K', vim.lsp.buf.hover, bufopts)
    vim.keymap.set('n', '<space>ca', vim.lsp.buf.code_action, bufopts)
  end,
})

-- Auto-detect .jsa files
vim.api.nvim_create_autocmd({"BufRead", "BufNewFile"}, {
  pattern = {"*.jsa", "*.jsasta"},
  callback = function()
    vim.bo.filetype = "jsasta"
  end,
})
```

Or use the provided config:

```bash
cp editors/nvim-lsp-config.lua ~/.config/nvim/lua/lsp/jsasta.lua
```

Then in your `init.lua`:

```lua
require('lsp.jsasta')
```

### Zed

Add to your Zed settings (`~/.config/zed/settings.json`):

```json
{
  "languages": {
    "JSasta": {
      "language_servers": ["jsasta-lsp"],
      "file_types": ["jsa", "jsasta"],
      "line_comments": ["// "],
      "block_comments": [["/*", "*/"]],
      "brackets": [
        { "start": "{", "end": "}", "close": true, "newline": true },
        { "start": "[", "end": "]", "close": true, "newline": true },
        { "start": "(", "end": ")", "close": true, "newline": false }
      ]
    }
  },
  "lsp": {
    "jsasta-lsp": {
      "binary": {
        "path": "jsastad",
        "arguments": ["--stdio"]
      }
    }
  }
}
```

Or copy the provided config:

```bash
cat editors/zed-config.json >> ~/.config/zed/settings.json
```

### Helix

Add to your `~/.config/helix/languages.toml`:

```toml
[[language]]
name = "jsasta"
scope = "source.jsasta"
file-types = ["jsa", "jsasta"]
roots = [".git"]
comment-token = "//"
language-server = { command = "jsastad", args = ["--stdio"] }
indent = { tab-width = 4, unit = "    " }
```

### Emacs (with LSP Mode)

Add to your Emacs config:

```elisp
(with-eval-after-load 'lsp-mode
  (add-to-list 'lsp-language-id-configuration '(jsasta-mode . "jsasta"))
  
  (lsp-register-client
   (make-lsp-client
    :new-connection (lsp-stdio-connection '("jsastad" "--stdio"))
    :major-modes '(jsasta-mode)
    :server-id 'jsasta-lsp)))

(add-hook 'jsasta-mode-hook #'lsp-deferred)
```

### Sublime Text (with LSP Package)

Install the LSP package, then add to your Sublime settings:

```json
{
  "clients": {
    "jsasta": {
      "enabled": true,
      "command": ["jsastad", "--stdio"],
      "selector": "source.jsasta",
      "languageId": "jsasta"
    }
  }
}
```

## Testing the LSP

### Manual Test

You can test the LSP server manually:

```bash
jsastad --stdio
```

Then send a JSON-RPC initialize request (Ctrl+D to send):

```json
Content-Length: 123

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":null,"rootUri":"file:///tmp","capabilities":{}}}
```

You should receive an initialize response with server capabilities.

### Test with a Real Editor

1. Create a test JSasta file:

```javascript
// test.jsa
struct Point {
    x: i32;
    y: i32;
}

function main(): void {
    var p: Point = { x: 10, y: 20 };
    printf("%d %d\n", p.x, p.y);
}
```

2. Open it in your editor
3. You should see:
   - Syntax highlighting
   - No errors (if the code is correct)
   - Hover over `Point` should show type info
   - Completion after typing `p.`

### Debugging

Enable verbose logging:

**VS Code:**
```json
{
  "jsasta.lsp.trace.server": "verbose"
}
```

Check the Output panel (View → Output → "JSasta Language Server")

**Neovim:**
```lua
vim.lsp.set_log_level("debug")
```

Check `:LspLog`

**Check Server Logs:**

The LSP server writes logs to stderr:

```bash
jsastad --stdio 2> /tmp/jsastad.log
```

Then check `/tmp/jsastad.log`

## Architecture

### Components

```
┌─────────────┐
│   Editor    │  (VS Code, Nvim, Zed, etc.)
└──────┬──────┘
       │ LSP Protocol (JSON-RPC over stdio)
       │
┌──────▼──────┐
│   jsastad   │  Language Server
│             │
│  ┌────────┐ │
│  │ Parser │ │  Parse .jsa files
│  └────┬───┘ │
│       │     │
│  ┌────▼────┐│
│  │  Type   ││  Type inference & checking
│  │ System  ││
│  └─────────┘│
└─────────────┘
```

### Protocol Flow

1. **Initialize**: Editor sends `initialize` request with capabilities
2. **Initialized**: Server responds with its capabilities
3. **didOpen**: Editor notifies server of opened document
4. **Parse & Analyze**: Server parses and type-checks the document
5. **publishDiagnostics**: Server sends errors/warnings to editor
6. **didChange**: Editor sends document changes
7. **Requests**: Editor requests hover, completion, definition, etc.
8. **shutdown**: Editor requests shutdown
9. **exit**: Connection closes

### Key Files

- `src/lsp/lsp_server.h` - Main LSP server interface
- `src/lsp/lsp_protocol.c` - JSON-RPC protocol implementation
- `src/lsp/lsp_json.c` - Minimal JSON parser/builder
- `src/lsp/lsp_handlers.c` - LSP method handlers
- `src/lsp/lsp_server_impl.c` - Document management & parsing
- `src/lsp/lsp_main.c` - Entry point

## Capabilities

Current server capabilities sent in initialize response:

```json
{
  "capabilities": {
    "textDocumentSync": 1,
    "hoverProvider": true,
    "completionProvider": {
      "triggerCharacters": ["."]
    },
    "definitionProvider": true
  }
}
```

## Contributing

To add new LSP features:

1. Add capability flag in `lsp_server.h`
2. Implement handler in `lsp_handlers.c`
3. Register handler in `lsp_server_impl.c`
4. Update capabilities in initialize response
5. Test with multiple editors

## Troubleshooting

### Server Not Starting

```bash
# Check if jsastad exists
which jsastad

# Try running manually
jsastad --stdio

# Check for errors
jsastad --stdio 2>&1
```

### No Diagnostics Appearing

- Check that the file is recognized as JSasta (`.jsa` extension)
- Verify server is connected (check editor's LSP status)
- Try saving the file to trigger re-analysis
- Check server logs for errors

### Completion Not Working

- Make sure you're typing after a trigger character (`.`)
- Check that `completionProvider` capability is enabled
- This feature is still being developed

### Performance Issues

The LSP server reparses the entire file on every change (full sync). For large files, this might be slow. Future optimization will add incremental sync.

## Resources

- [LSP Specification](https://microsoft.github.io/language-server-protocol/)
- [VS Code Extension API](https://code.visualstudio.com/api)
- [nvim-lspconfig](https://github.com/neovim/nvim-lspconfig)
- [Zed LSP](https://zed.dev/docs/extensions/languages)

## License

MIT
