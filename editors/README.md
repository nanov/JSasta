# JSasta Editor Support

Syntax highlighting and editor support for the JSasta programming language.

## Features

- Syntax highlighting for JSasta keywords, types, and constructs
- Support for TypeScript-style type annotations
- Object type syntax highlighting
- Comment highlighting (single-line `//` and multi-line `/* */`)
- Function declaration and call highlighting
- Operator and punctuation highlighting

## Supported Editors

- [Visual Studio Code](#visual-studio-code)
- [Zed](#zed)
- [Sublime Text](#sublime-text)
- [Vim/Neovim](#vimneovim)
- [Emacs](#emacs)

---

## Visual Studio Code

### Installation

#### Method 1: Manual Installation

1. Copy the `vscode` folder to your VS Code extensions directory:
   ```bash
   # Linux/macOS
   cp -r vscode ~/.vscode/extensions/jsasta-syntax
   
   # Windows
   xcopy vscode %USERPROFILE%\.vscode\extensions\jsasta-syntax\ /E /I
   ```

2. Reload VS Code (`Ctrl+Shift+P` → "Developer: Reload Window")

#### Method 2: Symlink (for development)

```bash
# Linux/macOS
ln -s "$(pwd)/vscode" ~/.vscode/extensions/jsasta-syntax

# Windows (requires admin)
mklink /D "%USERPROFILE%\.vscode\extensions\jsasta-syntax" "%CD%\vscode"
```

### Usage

- Files with `.js` or `.jsasta` extension will automatically use JSasta syntax
- Change language mode: `Ctrl+K M` → type "jsasta"

### Features

- Full syntax highlighting
- Auto-closing brackets and quotes
- Comment toggling (`Ctrl+/`)
- Block folding support

---

## Sublime Text

### Installation

1. Open Sublime Text
2. Go to `Preferences` → `Browse Packages...`
3. Create a `JSasta` folder in the `Packages` directory
4. Copy `sublime/JSasta.sublime-syntax` to the `JSasta` folder:
   ```bash
   # Linux/macOS
   cp sublime/JSasta.sublime-syntax ~/Library/Application\ Support/Sublime\ Text/Packages/JSasta/
   
   # Windows
   copy sublime\JSasta.sublime-syntax %APPDATA%\Sublime Text\Packages\JSasta\
   ```

5. Restart Sublime Text

### Usage

- Open a `.js` or `.jsasta` file
- Change syntax: `View` → `Syntax` → `JSasta`
- Or use `Ctrl+Shift+P` → "Set Syntax: JSasta"

---

## Vim/Neovim

### Installation

#### Vim

```bash
# Create syntax directory if it doesn't exist
mkdir -p ~/.vim/syntax

# Copy syntax file
cp vim/jsasta.vim ~/.vim/syntax/

# Add to ~/.vimrc
echo "au BufRead,BufNewFile *.js,*.jsasta set filetype=jsasta" >> ~/.vimrc
```

#### Neovim

```bash
# Create syntax directory if it doesn't exist
mkdir -p ~/.config/nvim/syntax

# Copy syntax file
cp vim/jsasta.vim ~/.config/nvim/syntax/

# Add to ~/.config/nvim/init.vim
echo "au BufRead,BufNewFile *.js,*.jsasta set filetype=jsasta" >> ~/.config/nvim/init.vim
```

### Usage

- Open any `.js` or `.jsasta` file
- Syntax highlighting will be applied automatically
- Force filetype: `:set filetype=jsasta`

---

## Emacs

### Installation

1. Copy the Emacs mode file:
   ```bash
   cp emacs/jsasta-mode.el ~/.emacs.d/
   ```

2. Add to your `~/.emacs` or `~/.emacs.d/init.el`:
   ```elisp
   (add-to-list 'load-path "~/.emacs.d/")
   (require 'jsasta-mode)
   ```

3. Restart Emacs or evaluate the configuration: `M-x eval-buffer`

### Usage

- Open a `.js` or `.jsasta` file
- Mode will be activated automatically
- Manually activate: `M-x jsasta-mode`

### Features

- Syntax highlighting
- Auto-indentation
- Comment commands (`M-;`)
- Tab width: 4 spaces

---

## Syntax Examples

Here are some examples of JSasta syntax that will be highlighted:

### Basic Types and Variables

```javascript
var count: int = 42;
var temperature: double = 98.6;
var name: string = "Alice";
var isActive: bool = true;
```

### Functions with Type Annotations

```javascript
function add(a: int, b: int): int {
    return a + b;
}

function greet(name: string): string {
    return "Hello, " + name;
}
```

### Object Types

```javascript
var person: { name: string, age: int } = {
    name: "Bob",
    age: 30,
};

function getAge(p: { name: string, age: int }): int {
    return p.age;
}
```

### Control Flow

```javascript
if (count > 0) {
    console.log("Positive");
} else {
    console.log("Non-positive");
}

while (count < 10) {
    count++;
}

for (var i: int = 0; i < 5; i++) {
    console.log(i);
}
```

---

## Color Themes

The syntax files use standard TextMate scopes that work with most color themes:

- `keyword.control` - Control flow keywords (if, else, while, etc.)
- `keyword.other` - Declaration keywords (var, function, etc.)
- `storage.type` - Type names (int, string, bool, etc.)
- `string.quoted` - String literals
- `constant.numeric` - Numbers
- `comment.line` / `comment.block` - Comments
- `entity.name.function` - Function names
- `variable.other.property` - Object properties

---

## Contributing

To contribute syntax highlighting improvements:

1. Edit the appropriate syntax file for your editor
2. Test with various JSasta code samples
3. Submit a pull request with your changes

### Testing Checklist

- [ ] Keywords highlighted correctly
- [ ] Type annotations work (`:` followed by type)
- [ ] Object types work (`{ prop: type }`)
- [ ] Strings and numbers highlighted
- [ ] Comments (single and multi-line) highlighted
- [ ] Function declarations and calls highlighted
- [ ] Operators highlighted properly

---

## Troubleshooting

### VS Code

**Problem**: Syntax highlighting not working
- Solution: Reload window (`Ctrl+Shift+P` → "Developer: Reload Window")
- Check that extension is installed: `~/.vscode/extensions/jsasta-syntax`

**Problem**: Wrong language detected
- Solution: Manually set language (`Ctrl+K M` → "jsasta")

### Sublime Text

**Problem**: Syntax not appearing in menu
- Solution: Restart Sublime Text after installation
- Check file location: `Preferences` → `Browse Packages` → `JSasta`

### Vim

**Problem**: No highlighting
- Solution: Check filetype is set: `:set filetype?`
- Manually set: `:set filetype=jsasta`
- Verify syntax file exists: `~/.vim/syntax/jsasta.vim`

### Emacs

**Problem**: Mode not loading
- Solution: Check load path includes directory with `jsasta-mode.el`
- Manually load: `M-x load-file RET ~/.emacs.d/jsasta-mode.el RET`
- Check `*Messages*` buffer for errors

---

## License

These editor support files are provided as-is for use with the JSasta compiler.

## Links

- [JSasta Compiler Repository](https://github.com/yourusername/JSasta)
- [Language Documentation](../README.md)

---

**Last Updated**: 2025
**Version**: 1.0.0
