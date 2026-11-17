const vscode = require("vscode");

const keywords = [
  "def", "class", "struct", "enum", "if", "elif", "else", "while", "for",
  "in", "match", "case", "try", "catch", "except", "finally", "export",
  "let", "const", "raise", "return", "break", "continue", "import", "end",
  "and", "or", "not", "as"
];

const types = ["string", "str", "number", "int", "bool", "char", "region",
  "bytes", "json", "ptr", "list", "dict", "tuple", "AnError", "cppCatcher"];

function parameter(name, type, optional = false, description = "") {
  return { name, type, optional, description };
}

function api(parameters, documentation, insertCount) {
  return { parameters, documentation, insertCount };
}

const builtins = {
  print: api([parameter("value", "any"), parameter("%v", "any", true)],
    "Print a value. For formatted output, use a plain string with `%v` placeholders and named `%v=value` arguments.", 1),
  printc: api([parameter("text", "string"), parameter("color", "string"),
    parameter("format", "string", true, "Optional color format: ansi or rgb.")],
  "Print text in a named, ANSI, or RGB color.", 2),
  input: api([parameter("prompt", "string"),
    parameter("type", "string", true, "string (default), number, int, bool, or char.")],
  "Read input and convert it to the requested scalar type.", 1),
  len: api([parameter("value", "string | bytes | list | dict | json")],
    "Return the number of items or bytes in a supported value."),
  sum: api([parameter("values", "list[int] | list[number]")],
    "Add all numbers in a numeric list."),
  all: api([parameter("values", "list[bool]")],
    "Return true if every item in the list is true."),
  any: api([parameter("values", "list[bool]")],
    "Return true if at least one item in the list is true."),
  min: api([parameter("left", "number | int"), parameter("right", "number | int")],
    "Return the smaller of two numeric values."),
  max: api([parameter("left", "number | int"), parameter("right", "number | int")],
    "Return the larger of two numeric values."),
  round: api([parameter("value", "number | int"),
    parameter("precision", "int", true, "Number of decimal places.")],
  "Round a number, optionally to a given number of decimal places.", 1),
  abs: api([parameter("value", "number | int")], "Return the absolute value."),
  floor: api([parameter("value", "number | int")], "Round down to an integer value."),
  ceil: api([parameter("value", "number | int")], "Round up to an integer value."),
  trunc: api([parameter("value", "number | int")], "Remove the fractional part."),
  int: api([parameter("value", "number | int | string")], "Convert a value to int."),
  number: api([parameter("value", "number | int | string")], "Convert a value to number."),
  char: api([parameter("value", "string")], "Convert a one-code-point string to char."),
  istype: api([parameter("value", "any"), parameter("type", "type")],
    "Check whether a value has the requested Sun type."),
  assert: api([parameter("condition", "bool")], "Raise a runtime error if the condition is false."),
  range: api([parameter("start", "number | int"), parameter("stop", "number | int"),
    parameter("step", "number | int", true)], "Generate values from start up to (but not including) stop.", 2),
  open: api([parameter("path", "string"), parameter("mode", "string")],
    "Open a file. Modes: r, w, a, rb, or pw."),
  read: api([parameter("handle", "int")], "Read all contents from an open file handle."),
  write: api([parameter("handle", "int"), parameter("contents", "string")],
    "Write text to a file handle opened in w or a mode."),
  close: api([parameter("handle", "int")], "Close an open file handle."),
  pw: api([parameter("handle", "int"), parameter("line", "int | number"),
    parameter("contents", "string")], "Replace one line in a file handle opened in pw mode.")
};

const mathFunctions = ["abs", "acos", "asin", "atan", "atan2", "ceil", "cos",
  "exp", "factorial", "fatorial", "floor", "fmod", "hypot", "log", "log10", "log2", "pow", "sin",
  "sqrt", "tan", "trunc"];

const mathApi = Object.fromEntries(mathFunctions.map(name => [
  `math.${name}`,
  api(({
    atan2: [parameter("y", "number | int"), parameter("x", "number | int")],
    fmod: [parameter("x", "number | int"), parameter("y", "number | int")],
    hypot: [parameter("x", "number | int"), parameter("y", "number | int")],
    pow: [parameter("base", "number | int"), parameter("exponent", "number | int")]
  })[name] || [parameter("value", "number | int")],
  name === "factorial" || name === "fatorial"
    ? "Calculate the generalized factorial using the gamma function; returns number."
    : `Return the ${name} mathematical result.`)
]));
const moduleApis = {
  io: {
    "io.open": api([parameter("path", "string"), parameter("mode", "string")], "Open a file handle. Modes: r, w, a, rb, or pw."),
    "io.read": api([parameter("handle", "int")], "Read all contents from an open file handle."),
    "io.write": api([parameter("handle", "int"), parameter("contents", "string")], "Write text to an open file handle."),
    "io.close": api([parameter("handle", "int")], "Close an open file handle."),
    "io.pw": api([parameter("handle", "int"), parameter("line", "int | number"), parameter("contents", "string")], "Replace one line in a file handle opened in pw mode."),
    "io.write_text": api([parameter("path", "string"), parameter("contents", "string")], "Create or replace a text file."),
    "io.append_text": api([parameter("path", "string"), parameter("contents", "string")], "Append text to a file."),
    "io.write_bytes": api([parameter("path", "string"), parameter("contents", "bytes")], "Write bytes to a binary file."),
    "io.read_text": api([parameter("path", "string")], "Read a complete text file."),
    "io.read_bytes": api([parameter("path", "string")], "Read a complete file as bytes."),
    "io.exists": api([parameter("path", "string")], "Check whether a path exists."),
    "io.read_lines": api([parameter("path", "string")], "Read a file as list[string]."),
    "io.is_directory": api([parameter("path", "string")], "Check whether a path is a directory."),
    "io.remove": api([parameter("path", "string")], "Remove a file or empty directory."),
    "io.create_directories": api([parameter("path", "string")], "Create a directory and its parents."),
    "io.createdir": api([parameter("path", "string"), parameter("exist", "string", true)], "Create a directory; exist may be \"ok\" or \"no\".", 1),
    "io.file_size": api([parameter("path", "string")], "Return a file's size in bytes.")
  },
  sys: {
    "sys.args": api([], "Return command-line arguments."),
    "sys.platform": api([], "Return the operating system name."),
    "sys.cwd": api([], "Return the current working directory."),
    "sys.env": api([parameter("name", "string"), parameter("fallback", "string")], "Read an environment variable or return fallback."),
    "sys.env_exists": api([parameter("name", "string")], "Check whether an environment variable is set."),
    "sys.exit": api([parameter("code", "int")], "Exit the process with a status code.")
  },
  memory: {
    "memory.alloc": api([parameter("size", "number | int")], "Allocate a managed memory region."),
    "memory.alloc_ptr": api([parameter("size", "number | int")], "Allocate an opaque pointer."),
    "memory.size": api([parameter("region", "region")], "Return a memory region's size."),
    "memory.read_u8": api([parameter("region", "region"), parameter("offset", "number | int")], "Read one byte."),
    "memory.read_u16": api([parameter("region", "region"), parameter("offset", "number | int"), parameter("endian", "string")], "Read a 16-bit integer; endian is little or big."),
    "memory.read_u32": api([parameter("region", "region"), parameter("offset", "number | int"), parameter("endian", "string")], "Read a 32-bit integer; endian is little or big."),
    "memory.write_u8": api([parameter("region", "region"), parameter("offset", "number | int"), parameter("value", "number | int")], "Write one byte."),
    "memory.write_u16": api([parameter("region", "region"), parameter("offset", "number | int"), parameter("value", "number | int"), parameter("endian", "string")], "Write a 16-bit integer; endian is little or big."),
    "memory.write_u32": api([parameter("region", "region"), parameter("offset", "number | int"), parameter("value", "number | int"), parameter("endian", "string")], "Write a 32-bit integer; endian is little or big."),
    "memory.fill": api([parameter("region", "region"), parameter("value", "number | int")], "Fill a region with a byte value."),
    "memory.copy": api([parameter("destination", "region"), parameter("destination_offset", "number | int"), parameter("source", "region"), parameter("source_offset", "number | int"), parameter("length", "number | int")], "Copy a byte range between regions."),
    "memory.free_ptr": api([parameter("pointer", "ptr")], "Release a pointer allocation."),
    "memory.ptr_size": api([parameter("pointer", "ptr")], "Return the size of a pointer allocation."),
    "memory.ptr_is_null": api([parameter("pointer", "ptr")], "Check whether a pointer is null.")
  },
  json: {
    "json.loads": api([parameter("text", "string")], "Parse JSON text."),
    "json.dumps": api([parameter("value", "json | scalar | list | dict")], "Serialize a supported value to JSON.")
  },
  bytes: {
    "bytes.from_string": api([parameter("text", "string")], "Copy a string's bytes into a bytes value."),
    "bytes.to_string": api([parameter("value", "bytes")], "Copy bytes into a string."),
    "bytes.from_list": api([parameter("values", "list[int]")], "Create bytes from integer octets."),
    "bytes.to_list": api([parameter("value", "bytes")], "Convert bytes to list[int]."),
    "bytes.from_region": api([parameter("region", "region")], "Copy a memory region into bytes."),
    "bytes.to_region": api([parameter("value", "bytes")], "Copy bytes into a memory region.")
  }
};

const apiSignatures = new Map(Object.entries(builtins));
for (const [name, value] of Object.entries(mathApi)) apiSignatures.set(name, value);
for (const entries of Object.values(moduleApis)) {
  for (const [name, value] of Object.entries(entries)) apiSignatures.set(name, value);
}

const methodApis = {
  string: {
    upper: api([], "Return an uppercase copy."),
    lower: api([], "Return a lowercase copy."),
    trim: api([], "Return a copy without surrounding whitespace."),
    split: api([parameter("separator", "string")], "Split into list[string]."),
    replace: api([parameter("search", "string"), parameter("replacement", "string")], "Replace all occurrences of search."),
    contains: api([parameter("text", "string")], "Check whether the string contains text."),
    starts_with: api([parameter("prefix", "string")], "Check whether the string starts with prefix."),
    ends_with: api([parameter("suffix", "string")], "Check whether the string ends with suffix.")
  },
  bytes: {
    contains: api([parameter("value", "number | int")], "Check whether the byte value occurs."),
    append: api([parameter("value", "number | int")], "Append one byte."),
    clear: api([], "Remove all bytes.")
  },
  list: {
    contains: api([parameter("value", "item")], "Check whether the list contains a value."),
    remove: api([parameter("value", "item")], "Remove a matching list item."),
    pop: api([parameter("index", "number | int")], "Remove and return the item at index."),
    append: api([parameter("value", "item")], "Append an item to the list."),
    insert: api([parameter("index", "number | int"), parameter("value", "item")], "Insert an item at index."),
    clear: api([], "Remove all items from the list.")
  },
  dict: {
    contains: api([parameter("key", "key")], "Check whether a key exists."),
    remove: api([parameter("key", "key")], "Remove a key and its value."),
    get: api([parameter("key", "key"), parameter("default", "value")], "Return the value or a default."),
    keys: api([], "Return the dictionary keys."),
    values: api([], "Return the dictionary values."),
    clear: api([], "Remove all entries.")
  },
  json: {
    as_string: api([], "Convert this JSON value to string."),
    as_number: api([], "Convert this JSON value to number."),
    as_int: api([], "Convert this JSON value to int."),
    as_bool: api([], "Convert this JSON value to bool."),
    is_null: api([], "Check whether this JSON value is null."),
    size: api([], "Return the number of contained items.")
  }
};

const completionTriggers = [
  ..."abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_ ."
];

function completion(label, detail, kind, insertText) {
  const item = new vscode.CompletionItem(label, kind);
  item.detail = detail;
  if (insertText) item.insertText = insertText;
  return item;
}

function signatureLabel(name, definition) {
  return `${name}(${definition.parameters.map(item =>
    `${item.name}: ${item.type}${item.optional ? " (optional)" : ""}`).join(", ")})`;
}

function signatureItem(name, definition) {
  const item = new vscode.SignatureInformation(
    signatureLabel(name, definition), definition.documentation);
  item.parameters = definition.parameters.map(parameter =>
    new vscode.ParameterInformation(
      `${parameter.name}: ${parameter.type}${parameter.optional ? " (optional)" : ""}`,
      parameter.description));
  return item;
}

function snippetFor(name, definition) {
  const count = definition.insertCount === undefined
    ? definition.parameters.filter(item => !item.optional).length
    : definition.insertCount;
  const args = definition.parameters.slice(0, count).map((item, index) =>
    `\${${index + 1}:${item.name}}`);
  return `${name}(${args.length ? `${args.join(", ")}` : "$0"}${args.length ? "$0" : ""})`;
}

function readDocumentPrefix(document, position) {
  return document.getText(new vscode.Range(0, 0, position.line, position.character));
}

function getMemberType(document, target) {
  const escaped = target.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  const source = document.getText();
  const declaration = new RegExp(
    String.raw`^\s*(?:let|const)\s+${escaped}\s*:\s*([A-Za-z_]\w*)`,
    "m").exec(source);
  const functionParameter = new RegExp(
    String.raw`\bdef\s+[A-Za-z_]\w*\s*\([^)]*\b${escaped}\s*:\s*([A-Za-z_]\w*)`,
    "m").exec(source);
  const type = declaration?.[1] || functionParameter?.[1];
  if (!type) return undefined;
  if (type.startsWith("list")) return "list";
  if (type.startsWith("dict")) return "dict";
  return ({ str: "string", string: "string" })[type] || type;
}

function moduleCompletionItems(moduleName) {
  const entries = moduleApis[moduleName] || {};
  return Object.entries(entries).map(([qualifiedName, definition]) => {
    const name = qualifiedName.slice(moduleName.length + 1);
    return completion(name, `${signatureLabel(name, definition)} — ${definition.documentation}`,
      vscode.CompletionItemKind.Function,
      new vscode.SnippetString(snippetFor(name, definition)));
  });
}

function provideCompletions(document, position) {
  const beforeCursor = readDocumentPrefix(document, position);
  const moduleContext = beforeCursor.match(/\b(math|io|sys|memory|json|bytes)\.[A-Za-z_]*$/);
  if (moduleContext) {
    const moduleName = moduleContext[1];
    if (moduleName === "math") {
      return Object.entries(mathApi).map(([qualifiedName, definition]) => {
        const name = qualifiedName.slice(5);
        return completion(name, `${signatureLabel(name, definition)} — ${definition.documentation}`,
          vscode.CompletionItemKind.Function,
          new vscode.SnippetString(snippetFor(name, definition)));
      });
    }
    return moduleCompletionItems(moduleName);
  }
  const memberContext = beforeCursor.match(/([A-Za-z_]\w*)\.[A-Za-z_]*$/);
  if (memberContext) {
    const type = getMemberType(document, memberContext[1]);
    const methods = methodApis[type] || {};
    return Object.entries(methods).map(([name, definition]) =>
      completion(name, `${signatureLabel(name, definition)} — ${definition.documentation}`,
        vscode.CompletionItemKind.Method,
        new vscode.SnippetString(snippetFor(name, definition))));
  }

  const items = [];
  const importContext = beforeCursor.match(/^\s*import\s+[\w, ]*$/);
  if (importContext) {
    for (const name of ["math", "io", "sys", "memory", "json"]) {
      items.push(completion(name, "Sun standard library module",
        vscode.CompletionItemKind.Module));
    }
    return items;
  }
  const typeContext = beforeCursor.match(/:\s*[A-Za-z_]*$/);
  if (typeContext) {
    for (const type of types) {
      items.push(completion(type, "Sun type", vscode.CompletionItemKind.Class));
    }
    return items;
  }
  for (const word of keywords) {
    items.push(completion(word, "Sun keyword", vscode.CompletionItemKind.Keyword));
  }
  for (const type of types) {
    items.push(completion(type, "Sun type", vscode.CompletionItemKind.Class));
  }
  for (const [name, definition] of Object.entries(builtins)) {
    items.push(completion(name, `${signatureLabel(name, definition)} — ${definition.documentation}`,
      vscode.CompletionItemKind.Function,
      new vscode.SnippetString(snippetFor(name, definition))));
  }
  for (const [name, definition] of Object.entries(mathApi)) {
    items.push(completion(name, `${signatureLabel(name, definition)} — ${definition.documentation}`,
      vscode.CompletionItemKind.Function,
      new vscode.SnippetString(snippetFor(name, definition))));
  }
  for (const name of ["math", "io", "sys", "memory", "json", "bytes"]) {
    items.push(completion(name, "Sun standard module", vscode.CompletionItemKind.Module));
  }
  for (const [moduleName, entries] of Object.entries(moduleApis)) {
    for (const [qualifiedName, definition] of Object.entries(entries)) {
      items.push(completion(qualifiedName,
        `${signatureLabel(qualifiedName, definition)} — ${definition.documentation}`,
        vscode.CompletionItemKind.Function,
        new vscode.SnippetString(snippetFor(qualifiedName, definition))));
    }
  }

  const source = document.getText();
  const declaration = /^\s*(?:export\s+)?(?:def|class|struct|enum)\s+([A-Za-z_]\w*)/gm;
  const variable = /^\s*(?:let|const)\s+([A-Za-z_]\w*)\s*:/gm;
  const parameter = /\bdef\s+[A-Za-z_]\w*\s*\(([^)]*)\)/gm;
  const seen = new Set();
  for (const pattern of [declaration, variable]) {
    let match;
    while ((match = pattern.exec(source)) !== null) {
      if (!seen.has(match[1])) {
        seen.add(match[1]);
        items.push(completion(match[1], "Declared in this file",
          vscode.CompletionItemKind.Variable));
      }
    }
  }
  let match;
  while ((match = parameter.exec(source)) !== null) {
    const names = match[1].matchAll(/\*{0,2}\s*([A-Za-z_]\w*)\s*:/g);
    for (const parameterMatch of names) {
      const name = parameterMatch[1];
      if (!seen.has(name)) {
        seen.add(name);
        items.push(completion(name, "Function parameter in this file",
          vscode.CompletionItemKind.Variable));
      }
    }
  }
  return items;
}

function splitParameters(text) {
  const parameters = [];
  let start = 0;
  let depth = 0;
  let quote = false;
  let escaped = false;
  for (let index = 0; index < text.length; index += 1) {
    const char = text[index];
    if (quote) {
      if (escaped) escaped = false;
      else if (char === "\\") escaped = true;
      else if (char === "\"") quote = false;
      continue;
    }
    if (char === "\"") quote = true;
    else if (char === "(" || char === "[" || char === "{") depth += 1;
    else if (char === ")" || char === "]" || char === "}") depth -= 1;
    else if (char === "," && depth === 0) {
      parameters.push(text.slice(start, index).trim());
      start = index + 1;
    }
  }
  if (text.slice(start).trim()) parameters.push(text.slice(start).trim());
  return parameters;
}

function findDefaultSeparator(text) {
  let depth = 0;
  let quote = false;
  let escaped = false;
  for (let index = 0; index < text.length; index += 1) {
    const char = text[index];
    if (quote) {
      if (escaped) escaped = false;
      else if (char === "\\") escaped = true;
      else if (char === "\"") quote = false;
      continue;
    }
    if (char === "\"") quote = true;
    else if (char === "(" || char === "[" || char === "{") depth += 1;
    else if (char === ")" || char === "]" || char === "}") depth -= 1;
    else if (char === "=" && depth === 0) return index;
  }
  return -1;
}

function declaredSignatures(documentText) {
  const signatures = new Map();
  const declaration = /^\s*(?:export\s+)?def\s+([A-Za-z_]\w*)\s*\(/gm;
  let match;
  while ((match = declaration.exec(documentText)) !== null) {
    const opening = declaration.lastIndex - 1;
    let depth = 1;
    let quote = false;
    let escaped = false;
    let closing = opening + 1;
    for (; closing < documentText.length && depth > 0; closing += 1) {
      const char = documentText[closing];
      if (quote) {
        if (escaped) escaped = false;
        else if (char === "\\") escaped = true;
        else if (char === "\"") quote = false;
      } else if (char === "\"") quote = true;
      else if (char === "(") depth += 1;
      else if (char === ")") depth -= 1;
    }
    if (depth !== 0) continue;
    const parameterText = documentText.slice(opening + 1, closing - 1);
    declaration.lastIndex = closing;
    const parameters = splitParameters(parameterText).map(text => {
      const equals = findDefaultSeparator(text);
      const head = (equals < 0 ? text : text.slice(0, equals)).trim();
      const colon = head.indexOf(":");
      const name = (colon < 0 ? head : head.slice(0, colon)).trim();
      const type = colon < 0 ? "any" : head.slice(colon + 1).trim();
      return parameter(name, type, equals >= 0);
    });
    signatures.set(match[1], api(parameters, "Function declared in this file."));
  }
  return signatures;
}

function findActiveCall(document, position) {
  const source = readDocumentPrefix(document, position);
  const stack = [];
  let quote = false;
  let escaped = false;
  let comment = false;
  for (let index = 0; index < source.length; index += 1) {
    const char = source[index];
    if (comment) {
      if (char === "\n") comment = false;
      continue;
    }
    if (quote) {
      if (escaped) escaped = false;
      else if (char === "\\") escaped = true;
      else if (char === "\"") quote = false;
      continue;
    }
    if (char === "#") comment = true;
    else if (char === "\"") quote = true;
    else if (char === "(" || char === "[" || char === "{") {
      stack.push({ char, index });
    } else if (char === ")" || char === "]" || char === "}") {
      const expected = char === ")" ? "(" : char === "]" ? "[" : "{";
      if (stack.length && stack[stack.length - 1].char === expected) stack.pop();
    }
  }
  const opening = [...stack].reverse().find(item => item.char === "(");
  if (!opening) return null;
  const callee = source.slice(0, opening.index)
    .match(/([A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*)\s*$/);
  if (!callee) return null;
  const callText = source.slice(opening.index + 1);
  let activeParameter = 0;
  let nested = 0;
  let inString = false;
  let stringEscape = false;
  let inComment = false;
  for (const char of callText) {
    if (inComment) {
      if (char === "\n") inComment = false;
      continue;
    }
    if (inString) {
      if (stringEscape) stringEscape = false;
      else if (char === "\\") stringEscape = true;
      else if (char === "\"") inString = false;
      continue;
    }
    if (char === "#") inComment = true;
    else if (char === "\"") inString = true;
    else if (char === "(" || char === "[" || char === "{") nested += 1;
    else if (char === ")" || char === "]" || char === "}") nested -= 1;
    else if (char === "," && nested === 0) activeParameter += 1;
  }
  return { name: callee[1], activeParameter };
}

function getSignature(document, name) {
  const exact = apiSignatures.get(name);
  if (exact) return exact;
  const member = name.match(/^([A-Za-z_]\w*)\.([A-Za-z_]\w*)$/);
  if (member) {
    const type = getMemberType(document, member[1]);
    const methods = methodApis[type];
    if (methods?.[member[2]]) return methods[member[2]];
  }
  return declaredSignatures(document.getText()).get(name);
}

function provideSignatureHelp(document, position) {
  const call = findActiveCall(document, position);
  if (!call) return null;
  const definition = getSignature(document, call.name);
  if (!definition) return null;
  const help = new vscode.SignatureHelp();
  help.signatures = [signatureItem(call.name, definition)];
  help.activeSignature = 0;
  help.activeParameter = definition.parameters.length
    ? Math.min(call.activeParameter, definition.parameters.length - 1)
    : 0;
  return help;
}

function provideHover(document, position) {
  const word = document.getWordRangeAtPosition(position);
  if (!word) return null;
  const name = document.getText(word);
  const linePrefix = readDocumentPrefix(document, position);
  const qualified = linePrefix.match(/([A-Za-z_]\w*)\.\w*$/);
  const key = qualified ? `${qualified[1]}.${name}` : name;
  const definition = getSignature(document, key);
  if (!definition) return null;
  const markdown = new vscode.MarkdownString();
  markdown.appendCodeblock(signatureLabel(key, definition), "sun");
  markdown.appendMarkdown(definition.documentation);
  return new vscode.Hover(markdown, word);
}

function getIndentLevel(line) {
  const text = line.trim();
  if (/^endcpp\s*(?:#.*)?$/.test(text)) return "closeCpp";
  if (/^end\s*(?:#.*)?$/.test(text)) return "close";
  if (/^inline\s+cpp:\s*$/.test(text)) return "cpp";
  if (/^(?:elif|else|except|finally|case)\b/.test(text)) return "branch";
  if (/^(?:def|class|struct|enum|if|while|for|match|try|with)\b/.test(text)) {
    return "open";
  }
  return "normal";
}

function formatDocument(document, indentSize) {
  const edits = [];
  const indent = " ".repeat(indentSize);
  const blocks = [];
  let inCpp = false;

  for (let index = 0; index < document.lineCount; index += 1) {
    const line = document.lineAt(index);
    const text = line.text;
    const trimmed = text.trim();
    if (inCpp && !/^endcpp(?:\s*(?:#.*)?)?$/.test(trimmed)) continue;

    const level = getIndentLevel(text);
    let lineDepth = blocks.length;
    if (level === "branch") {
      const branchName = trimmed.match(/^[A-Za-z]+/)[0];
      if (blocks[blocks.length - 1] === "case") blocks.pop();
      const enclosing = blocks[blocks.length - 1];
      if (branchName === "case" || (branchName === "else" && enclosing === "match")) {
        lineDepth = blocks.length;
        blocks.push("case");
      } else {
        lineDepth = Math.max(0, blocks.length - 1);
      }
    } else if (level === "close" || level === "closeCpp") {
      if (level === "close" && blocks[blocks.length - 1] === "case") {
        blocks.pop();
        if (blocks[blocks.length - 1] === "match") blocks.pop();
      } else if (blocks.length > 0) {
        blocks.pop();
      }
      if (level === "closeCpp") inCpp = false;
      lineDepth = blocks.length;
    }

    if (trimmed.length > 0) {
      const desired = indent.repeat(lineDepth);
      const leading = text.match(/^\s*/)[0];
      if (leading !== desired) {
        edits.push(vscode.TextEdit.replace(
          new vscode.Range(index, 0, index, leading.length), desired));
      }
    } else if (text.length > 0) {
      edits.push(vscode.TextEdit.replace(
        new vscode.Range(index, 0, index, text.length), ""));
    }

    if (level === "open") {
      blocks.push(trimmed.match(/^[A-Za-z]+/)[0]);
    } else if (level === "cpp") {
      blocks.push("cpp");
      if (level === "cpp") inCpp = true;
    }
  }
  return edits;
}

function activate(context) {
  context.subscriptions.push(
    vscode.languages.registerCompletionItemProvider(
      { language: "sun" },
      {
        provideCompletionItems(document, position) {
          return provideCompletions(document, position);
        }
      },
      ...completionTriggers
    ),
    vscode.languages.registerSignatureHelpProvider(
      { language: "sun" },
      {
        provideSignatureHelp(document, position) {
          return provideSignatureHelp(document, position);
        }
      },
      { triggerCharacters: ["(", ","], retriggerCharacters: [","] }
    ),
    vscode.languages.registerHoverProvider(
      { language: "sun" },
      {
        provideHover(document, position) {
          return provideHover(document, position);
        }
      }
    ),
    vscode.languages.registerDocumentFormattingEditProvider(
      { language: "sun" },
      {
        provideDocumentFormattingEdits(document) {
          const indentSize = vscode.workspace
            .getConfiguration("sun.format", document.uri)
            .get("indentSize", 4);
          const safeIndentSize = Number.isInteger(indentSize)
            ? Math.max(1, Math.min(8, indentSize))
            : 4;
          return formatDocument(document, safeIndentSize);
        }
      }
    )
  );
}

function deactivate() {}

module.exports = {
  activate,
  deactivate,
  findActiveCall,
  provideCompletions,
  provideHover,
  provideSignatureHelp
};
