const vscode = require("vscode");
const path = require("path");
const { LanguageClient, TransportKind } = require("vscode-languageclient");

let client;

function activate(context) {
	console.log("JSasta extension activating...");

	// Get the LSP server executable path
	// Try to find jsastad in PATH first, then look in common installation locations
	const serverCommand = findServerCommand();
	console.log("Server command found:", serverCommand);

	if (!serverCommand) {
		vscode.window.showWarningMessage(
			"JSasta LSP server (jsastad) not found. Please install it or add it to your PATH. " +
				"Language features will not be available.",
		);
		console.error("Could not find jsastad executable");
		return;
	}

	// Configure the language server
	const serverOptions = {
		command: serverCommand,
		args: ["--stdio"],
		transport: TransportKind.stdio,
	};

	// Configure the client options
	const clientOptions = {
		documentSelector: [{ scheme: "file", language: "jsasta" }],
		synchronize: {
			// Notify the server about file changes to .jsa files
			fileEvents: vscode.workspace.createFileSystemWatcher("**/*.jsa"),
		},
	};

	// Create the language client
	client = new LanguageClient(
		"jsastaLsp",
		"JSasta Language Server",
		serverOptions,
		clientOptions,
	);

	// Start the client (which will launch the server)
	client.start();

	console.log("JSasta extension activated!");
}

function deactivate() {
	if (!client) {
		return undefined;
	}
	return client.stop();
}

function findServerCommand() {
	// Check if jsastad is in PATH
	const { execSync } = require("child_process");

	try {
		// Try to find jsastad using 'which' (Unix) or 'where' (Windows)
		const command =
			process.platform === "win32" ? "where jsastad" : "which jsastad";
		const result = execSync(command, { encoding: "utf8" }).trim();
		if (result) {
			console.log("Found jsastad at:", result);
			return result.split("\n")[0]; // Take first result if multiple
		}
	} catch (e) {
		// Not found in PATH, try common locations
	}

	// Try common installation paths
	const commonPaths = [
		"/usr/local/bin/jsastad",
		"/usr/bin/jsastad",
		path.join(process.env.HOME || "", ".local", "bin", "jsastad"),
	];

	for (const p of commonPaths) {
		try {
			const fs = require("fs");
			if (fs.existsSync(p)) {
				console.log("Found jsastad at:", p);
				return p;
			}
		} catch (e) {
			continue;
		}
	}

	// Check VSCode settings for custom path
	const config = vscode.workspace.getConfiguration("jsasta");
	const customPath = config.get("lsp.serverPath");
	if (customPath) {
		console.log("Using custom jsastad path:", customPath);
		return customPath;
	}

	return null;
}

module.exports = {
	activate,
	deactivate,
};
