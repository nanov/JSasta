use zed_extension_api as zed;

struct JSastaExtension;

impl zed::Extension for JSastaExtension {
    fn new() -> Self {
        Self
    }

    fn language_server_command(
        &mut self,
        _language_server_id: &zed::LanguageServerId,
        _worktree: &zed::Worktree,
    ) -> zed::Result<zed::Command> {
        // JSasta doesn't have a language server yet
        // This can be implemented later when one is available
        Err("JSasta language server not implemented yet".to_string())
    }
}

zed::register_extension!(JSastaExtension);
