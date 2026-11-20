# Sun Language VS Code Extension

The extension adds `.su` syntax highlighting, snippets, completion suggestions,
a basic document formatter, and the Sun file icon theme.

## Install from source

In VS Code, run **Developer: Install Extension from Location...** and select
this `editors/vscode` directory. To use the file icon, select **Sun File Icons**
under **File Icon Theme**.

To install the packaged version, run **Extensions: Install from VSIX...** and
select `sun-language-0.3.2.vsix`, or run `code --install-extension
sun-language-0.3.2.vsix` from this directory.

The formatter uses four spaces by default. Change
`sun.format.indentSize` to use a different indent width. Inline C++ contents are
preserved verbatim.

Completion suggestions include Sun keywords and types, built-ins, standard
library functions, and declarations found in the current file. Function
suggestions show their signatures and insert placeholders for required
arguments. Member suggestions are selected from the receiver's declared type.

While editing a call, signature help shows its parameters and highlights the
active argument; it supports built-ins, standard library functions, and
functions declared in the current file. Hover over built-ins and standard
library functions for their signatures and short descriptions. Common
function, control-flow, class, exception, and inline C++ templates are also
available as snippets.

The formatter also recognizes `with` blocks and preserves inline C++ contents.
This extension provides editor features only; compiler backend options and
their current feature support are documented in the repository's
[language guide](../../HOWUSE.md) and [specification](../../SUN-SPEC.md).
