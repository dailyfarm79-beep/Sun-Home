# Sun

![Sun language logo](sunimg.jpg)

Sun e uma linguagem compilada experimental com arquivos `.su`. O compilador,
escrito em C++, traduz o subconjunto atual para C++17.

## Inicio rapido

```sh
make
build/sun examples/hello.su --compile
./examples/hello
```

O guia completo de instalacao, compilacao, sintaxe, exemplos e diagnostico esta
em [HOWUSE.md](HOWUSE.md).

## Recursos atuais

- Funcoes com parametros tipados (`string`, `number`, `bool`, `int`, `char`,
  listas e `region`) e blocos `end`.
- Chamadas de funcao, `print(...)` e `printc(...)` para texto com cores ANSI/RGB.
- Variaveis locais tipadas com `let`, constantes imutaveis com `const`,
  operadores aritmeticos e comparacoes.
- Operadores logicos com curto-circuito; loops `while` e `for ... in range(...)`.
- Listas homogeneas tipadas com indexacao, `append`, `insert`, `remove`,
  `pop`, `contains`, `clear`, `len` e iteracao.
- Dicionarios homogeneos com `keys`, `values`, `get`, `remove`, `contains`,
  `clear`, indexacao, `len` e iteracao.
- Tuplas heterogeneas, fixas e imutaveis com acesso por indice literal.
- `match` para valores escalares e variantes de enum; enums podem carregar campos tipados.
- Funcoes aninhadas com captura de variaveis externas por referencia e defaults
  constantes nos parametros.
- `input(prompt, tipo)`, `try`/`catch` legado e `except` tipado, com erros
  personalizados baseados em `AnError`, `raise` e `cppCatcher` para excecoes
  de blocos `inline cpp`; `finally` para executar limpeza ao sair do bloco.
- Inteiros assinados de 64 bits com conversoes verificadas e `char` Unicode.
- Funcoes com retornos tipados e atribuicao mutavel.
- `break`/`continue`, `istype(valor, tipo)` e parametros com valores padrao.
- Argumentos nomeados em funcoes/metodos Sun e construtores de tipos; parametros
  tipados `*args`/`**kwargs` para coletar argumentos extras, com desempacotamento
  de colecoes tipadas em chamadas.
- `sum`, `all`, `any`, `min`, `max` e `round` como funcoes integradas tipadas.
- `abs`, `floor`, `ceil`, `trunc` e `round(valor, casas)` para operacoes numericas.
- Condicoes `if`/`elif`/`else` e `import math` para funcoes matematicas.
- `import memory` com regioes de bytes gerenciadas, limites de alocacao e
  verificacao de bounds para leitura e escrita.
- `import io` para leitura de texto/linhas, escrita, diretorios e metadados de
  arquivos, handles com `open`/`close` e blocos `with open`.
- `import json` para `json.loads`/`json.dumps` e valores JSON dinamicos com
  objetos e arrays aninhados.
- `import sys` para argumentos, ambiente, diretorio atual, plataforma e saida.
- Imports de modulos locais com alias, structs, enums e classes com
  campos, metodos, `init` e heranca simples; `export def` controla a API publica
  de modulos, e classes de erro podem herdar de `AnError`.
- Blocos de interoperabilidade `inline cpp:` ... `endcpp`.
- Strings concatenadas com `+`, f-strings e operacoes de transformacao, busca,
  substituicao e divisao.
- Interpolacao `%v` com argumentos nomeados, como
  `print("Ola, %v", %v=nome)`.
- Erros de compilacao com arquivo, linha, coluna, trecho destacado e sugestoes.
- `--check` para validar sintaxe e tipos sem gerar C++.

Comentarios com `#` podem aparecer no fim de uma linha fora de strings. Sun
ainda esta em prototipo; classes comuns suportam heranca simples sem conversao
implicita para tipos base, e classes de erro herdam diretamente de `AnError`.

`inline cpp` executa codigo C++ nativo sem isolamento ou validacao de seguranca.
Use somente com codigo confiavel; nao e uma sandbox.

O core esta dividido em lexer, parser, gerador C++ e CLI em `src/`. Veja
[HOWUSE.md](HOWUSE.md) para detalhes, exemplos e regras do C++ inline.

## Visual Studio Code

Uma extensao para arquivos `.su`, com realce de sintaxe, snippets, sugestoes de
completude, formatacao basica e icone Sun, esta em
[`editors/vscode`](editors/vscode). No VS Code, abra essa pasta com
**Developer: Install Extension from Location...**. Para ver o icone dos arquivos
`.su`, selecione **Sun File Icons** em **File Icon Theme**.
