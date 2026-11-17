# Como usar a Sun

Este guia mostra como compilar o compilador, transpilar um arquivo `.su`,
gerar um executavel e escrever programas com a sintaxe atualmente suportada.
A referencia normativa das regras atuais esta em
[SUN-SPEC.md](SUN-SPEC.md); este arquivo continua sendo o guia pratico, com
exemplos e instrucoes de uso.

## 1. Requisitos

- Um compilador C++17: `g++` ou `clang++`.
- `make` para usar os comandos simplificados do projeto.
- Python 3 somente para executar os testes.

Nao sao necessarias bibliotecas de terceiros.

## 2. Compilar o compilador Sun

Na pasta raiz do projeto:

```sh
make
```

O executavel do compilador fica em `build/sun`. Para ver as opcoes:

```sh
build/sun --help
```

O core esta dividido em `src/main.cpp` (CLI), `src/lexer.cpp` (tokens),
`src/parser.cpp` (sintaxe e verificacao de tipos), `src/compiler.hpp` (AST e
tipos), `src/expression_codegen.cpp` (expressoes AST para C++) e
`src/codegen.cpp` (programa AST para C++). Expressoes tipadas sao representadas
pela AST e essa arvore e a fonte de verdade para sua emissao. A verificacao de
tipos ainda acontece durante o parsing. Declaracoes, atribuicoes, retornos,
chamadas, atribuicoes indexadas, chamadas de metodos, condicoes, loops e
bibliotecas imperativas ja possuem nos estruturados na AST. Apenas blocos
`inline cpp` sao preservados como texto C++ literal; eles continuam sendo uma
excecao explicitamente nativa. Essa separacao ja remove a maioria da conversao
direta de instrucoes para C++ durante o parsing, mas ainda nao divide o pipeline
em passes completos e independentes de lexing, analise semantica e codegen.

Para verificar sintaxe e tipos sem gravar C++ nem executar o compilador C++:

```sh
build/sun examples/hello.su --check
```

Em caso de erro, Sun mostra a linha e um trecho do codigo com um marcador de
posicao. `--check` nao pode ser combinado com `--compile`, `-o` ou `--compiler`.

## 3. Transpilar e compilar um programa

O exemplo incluido e `examples/hello.su`. Primeiro, gere o arquivo C++:

```sh
build/sun examples/hello.su
```

Isso cria `examples/hello.cpp`. Compile e execute o C++ manualmente:

```sh
g++ -std=c++17 examples/hello.cpp -o examples/hello
./examples/hello
```

Ou faca a transpilaçao e a compilacao em uma unica etapa:

```sh
build/sun examples/hello.su --compile
./examples/hello
```

O executavel gerado fica ao lado do arquivo `.su`, sem a extensao. Para
escolher o caminho do C++ gerado, use `-o`:

```sh
build/sun examples/hello.su -o build/hello.cpp
```

`--compiler clang++` escolhe outro compilador C++. Se essa opcao nao for
informada, Sun usa a variavel de ambiente `CXX` quando configurada e, caso
contrario, tenta usar `g++`.

### Backend LLVM experimental

O backend opcional `--emit-llvm` grava LLVM IR em um arquivo `.ll`; `--llvm`
tambem tenta compilar esse IR com `clang++` e cria o executavel ao lado do
arquivo `.su`:

```sh
cat > llvm-print.su <<'EOF'
let count: int = int(3)
count = count + int(4)
print(count)
EOF
build/sun llvm-print.su --emit-llvm
build/sun llvm-print.su --llvm
```

O subconjunto atual inclui variaveis escalares (`int`, `number`, `bool` e
`string`), atribuicao, aritmetica numerica, comparacoes, impressoes e blocos
`if`, `while`, `break` e `continue`. Funcoes, colecoes, chamadas gerais,
interpolacao e bibliotecas ainda nao estao implementadas nesse backend e geram
um erro explicito. Overflow inteiro e divisao invalida terminam o processo via
trap LLVM; ainda nao podem ser capturados como erros Sun. Use o backend C++ para
programas que precisem de recursos nao suportados ou da semantica completa de
tratamento de erros da Sun.

Para escrever o C++ gerado na saida padrao:

```sh
build/sun examples/hello.su -o -
```

## 4. Primeiro programa

Crie um arquivo chamado `ola.su`:

```sun
def cumprimentar(nome)
    print(f"Ola, {nome}!")
end

cumprimentar("mundo")
```

Compile e execute:

```sh
build/sun ola.su --compile
./ola
```

`def` inicia uma funcao e `end` encerra o bloco. A indentacao e recomendada
para facilitar a leitura, mas atualmente e `end` que determina onde o bloco
termina. Funcoes aceitam parametros de texto; chamadas podem aparecer antes ou
depois da declaracao da funcao.

Chamadas aceitam argumentos nomeados usando `nome=valor`. Eles podem vir depois
dos posicionais e podem ser escritos em qualquer ordem; parametros omitidos
usam seus valores padrao:

```sun
def greet(name: string, punctuation: string = "!")
    print("Hello, " + name + punctuation)
end

greet(punctuation="?", name="Ada")
```

O suporte se aplica a funcoes e metodos declarados em Sun e a construtores de
tipos Sun. Funcoes integradas, bibliotecas e metodos de colecao continuam
recebendo argumentos posicionais.

Uma funcao tambem pode receber argumentos extras com parametros tipados:
`*values: int` coleta os posicionais restantes em `list[int]`;
`**options: string` coleta nomes extras em `dict[string, string]`. Os tipos
dos valores extras sao escalares e explicitos. Desempacotamento na chamada
aceita uma lista tipada em `*values` (`func(*items)`) ou um dicionario de
chaves string em `**options` (`func(**options)`). O desempacotamento de
`**kwargs` requer que a funcao tenha somente esse parametro variadico; nao se
pode combinar um dicionario desempacotado com argumentos nomeados explicitos.
Para `*args`, os argumentos fixos devem vir antes da lista, sem outros valores
posicionais variadicos na mesma chamada:

```sun
def report(*values: int, **options: string)
    print(values)
    print(options)
end

report(int(3), int(5), label="total", mode="brief")
```

## 5. Strings e interpolacao

Strings usam aspas duplas. Os escapes aceitos sao `\\`, `\"`, `\n`, `\t`, `\r`
e `\0`; bytes NUL literais no arquivo fonte nao sao aceitos. O operador `+`
concatena texto:

```sun
def mostrar(nome)
    print("Ola, " + nome + "!")
end
```

Strings oferecem `upper()`, `lower()` e `trim()`; `split(separador)` retorna
`list[string]`; `replace(busca, substituicao)` retorna uma nova string;
`contains(texto)`, `starts_with(prefixo)` e `ends_with(sufixo)` retornam `bool`;
`count(texto)` retorna a quantidade de ocorrencias nao sobrepostas.
`split`, `replace` e `count` rejeitam o texto de busca vazio com erro de runtime.
As transformacoes de maiusculas/minusculas e `trim()` operam por byte usando o
locale C ativo; as demais operacoes preservam os bytes UTF-8 sem normalizacao.

```sun
let title: string = "  Sun language  ".trim().replace("Sun", "Moon")
print(title.upper())
print(title.split(" "))
```

### Impressao colorida

`printc(texto, cor[, formato])` imprime uma string ou f-string em cor e restaura
a cor padrao do terminal ao final. Cores nomeadas aceitas: `red`, `blue`,
`yellow`, `magenta` e `cyan`. Por padrao elas usam codigos ANSI; o terceiro
argumento pode ser `"rgb"` para usar ANSI truecolor ou `"ansi"` para selecionar
explicitamente a paleta ANSI:

```sun
let name: string = "Sun"
printc("Aviso", "yellow")
printc(f"Ola, {name}", "cyan", "rgb")
```

Tambem e possivel passar diretamente um codigo ANSI SGR entre aspas
(`"1;35"` ou `"\\033[35m"`), um RGB decimal (`"255,80,10"`) ou hexadecimal
(`"#FF500A"`). Esses codigos sao identificados automaticamente; o terceiro
argumento so se aplica a cores nomeadas. Os componentes RGB e os valores SGR
devem estar entre 0 e 255.

### F-strings

Coloque `f` antes das aspas e escreva expressoes Sun entre chaves:

```sun
def mostrar(nome)
    print(f"Ola, {nome}!")
    print(f"2 + 3 = {2 + 3}")
    print(f"Chaves literais: {{ e }}")
end
```

As expressoes interpoladas usam as regras normais de tipos e podem conter
operacoes, chamadas de funcao e acesso a itens. O resultado e convertido para
texto. `bytes` e exibido como uma lista de octetos; regioes mostram seu tamanho;
structs e classes mostram o tipo e seus campos (incluindo os herdados). Valores
`ptr` e erros personalizados nao podem ser interpolados diretamente. As chaves
duplas `{{` e `}}` representam chaves literais.

### Marcador `%v`

Para passar valores junto com uma string de formato, associe cada `%v` a um
argumento `%v=valor`:

```sun
def mostrar(nome)
    print("Seu nome e %v.", %v=nome)
    print("%v gosta de %v.", %v=nome, %v="Sun")
    print("Progresso: 100%%, %v esta online.", %v=nome)
end
```

Os marcadores sao substituidos da esquerda para a direita. Cada `%v` exige
exatamente um argumento `%v=...`. Os valores podem ser dos tipos suportados e
sao convertidos para texto. Use `%%` para escrever um sinal de porcentagem
literal quando estiver usando argumentos formatados.

## 6. Numeros, operadores, condicoes e matematica

Parametros podem declarar `string`, `number`, `bool`, `int`, `char` e tipos de
lista como `list[number]` ou `list[int]`; `region` e `ptr` dependem de
`import memory`. Parametros sem tipo continuam sendo `string` para manter
compatibilidade com programas anteriores. Variaveis locais usam `let` e exigem
tipo explicito:

```sun
import math

def classificar(x: number)
    let magnitude: number = math.sqrt(x * x)
    let pontuacao: number = magnitude + 2 * 3

    if pontuacao < 7:
        print("pequeno")
    elif pontuacao == 7:
        print("sete")
    else:
        print(f"grande: {pontuacao}")
    end
end

classificar(2)
```

As variaveis declaradas com `let` e os parametros podem ser reatribuidos com
`nome = expressao`; a expressao precisa ter o mesmo tipo:

```sun
def contar(): number
    let total: number = 0
    total = total + 1
    return total
end
```

Use `const nome: tipo = expressao` para declarar uma constante imutavel. O tipo
e explicito e o inicializador precisa ser uma expressao escalar que possa ser
calculada com seguranca durante a compilacao; outras constantes podem ser
usadas no calculo:

```sun
const base: int = int(20)
const resposta: int = base + int(22)
print(resposta)
```

O compilador substitui usos dessas constantes pelo valor calculado. Valores
mutaveis e operacoes que podem falhar, como divisao por zero, nao sao aceitos
como inicializadores de `const`.

Operadores aritmeticos: `+`, `-`, `*`, `/` e `%` (resto). `+` tambem concatena
duas strings. `int` e um inteiro assinado de 64 bits: sua divisao trunca em
direcao a zero e overflow ou divisao por zero geram erro em runtime.
`int(numero)` converte apenas valores inteiros dentro do intervalo de `int`;
`number(inteiro)` converte explicitamente para `number`, e `int` pode ser
promovido a `number` em expressoes. `number` usa `double` do C++ e tem precisao
finita; valores inteiros grandes podem perder exatidao quando convertidos.
`char` representa um unico code point
Unicode; `char("A")` e `char("é")` sao conversoes validas, enquanto strings
vazias ou com mais de um caractere sao rejeitadas.

Comparacoes disponiveis: `==`, `!=`, `<`, `<=`, `>` e `>=`. Comparacoes
ordenadas aceitam numeros; igualdade aceita valores do mesmo tipo e pares
`int`/`number`. Condicoes de `if`/`elif` precisam resultar em `bool`, por
exemplo uma comparacao, `true`/`false` ou um parametro booleano. Blocos `if`
podem ter varios `elif` e um `else`; todo o conjunto termina com `end`.

Operadores logicos `not`, `and` e `or` aceitam somente valores booleanos.
`not` tem precedencia acima de `and`, que tem precedencia acima de `or`.
`and` e `or` usam curto-circuito: a expressao da direita so e avaliada quando
necessario.

O compilador avalia expressoes constantes durante a compilacao, incluindo
operacoes aritmeticas, comparacoes e algumas funcoes matematicas e integradas.
Operacoes que podem falhar, como divisao por zero ou overflow de `int`, continuam
no programa gerado para preservar o erro em runtime.

Use `assert(condicao)` para verificar uma condicao `bool`. Se ela for falsa,
gera o erro de runtime `assertion failed`, que pode ser tratado com `try`/`catch`.

### Entrada tipada

`input(prompt, tipo)` mostra o prompt e le uma linha. Se o tipo for omitido,
`input(prompt)` retorna `string` (tambem aceita o alias `str`). Para conversao,
o tipo pode ser `number`, `int`, `bool` ou `char`. Valores invalidos geram erro
de runtime:

```sun
let nome: string = input("Nome: ")
let idade: int = input("Idade: ", int)
let ativo: bool = input("Ativo (true/false): ", bool)
let inicial: char = input("Inicial: ", char)
print(f"{nome}: {idade}, ativo={ativo}, inicial={inicial}")
```

`bool` aceita exatamente `true` ou `false`; `char` aceita exatamente um
code point UTF-8 valido.
O prompt precisa ser uma expressao `string`.

### Listas

Listas sao homogeneas, de uma dimensao e devem declarar seu tipo:

```sun
let notas: list[number] = [8.5, 9.0]
let valores: list[int] = []
valores.append(int(12))
valores.append(int(7))
valores[0] = int(10)

for valor in valores:
    print(valor)
end

print(len(notas))
```

Os tipos de elemento aceitos sao `number`, `int`, `string`, `bool` e `char`.
Indices comecam em zero; leitura e escrita fora dos limites, ou com indice
negativo/fracionario, geram erro de runtime. `append` exige um item do mesmo
tipo. `contains(item)` testa presenca. `remove(item)` apaga a primeira
ocorrencia e retorna `bool`. `insert(index, item)` aceita indices entre zero e
`len(lista)`, incluindo o final. `pop(index)` remove e retorna um item. Os
indices de `insert` e `pop` podem ser `int` ou `number`; valores fracionarios
ou fora do intervalo geram erro de runtime. `clear()` esvazia a lista.
`len` aceita listas e strings. Listas podem ser passadas e retornadas de
funcoes e impressas. Listas vazias precisam de uma anotacao de tipo; listas
aninhadas, listas de `region`/`ptr` e listas de tuplas nao sao suportadas.

### Funcoes integradas para colecoes e numeros

`sum(lista)` soma listas de `int` ou `number`; listas tipadas vazias resultam em
zero, e a soma de `int` detecta overflow. `all(lista_bool)` retorna `true`
quando todos os itens sao verdadeiros (inclusive para lista vazia), enquanto
`any(lista_bool)` retorna `true` quando pelo menos um item e verdadeiro.
`min(a, b)` e `max(a, b)` comparam numeros; argumentos mistos `int`/`number`
resultam em `number`, e valores `NaN` fazem o resultado ser `NaN`.
`round(valor)` arredonda para o inteiro mais proximo, retornando `number`; com
`round(valor, casas)` arredonda para o numero de casas decimais indicado por um
`int` (casas negativas arredondam para dezenas, centenas etc.). Empates como
`2.5` e `-2.5` arredondam para longe de zero. `abs`, `floor`, `ceil` e `trunc`
aceitam `number` ou `int` e retornam `number`; essas operacoes nao exigem
`import math`.

### Dicionarios

Dicionarios declaram os tipos homogeneos de chave e valor com
`dict[chave, valor]`. As chaves aceitas sao `string`, `int`, `bool` e `char`;
os valores podem ser `string`, `number`, `int`, `bool` ou `char`. Um dicionario
vazio precisa de anotacao de tipo:

```sun
let notas: dict[string, number] = {}
notas["Ada"] = 10
notas["Lin"] = 9.5
print(notas.contains("Ada"))
print(notas["Ada"])
print(len(notas))
let removida: bool = notas.remove("Lin")

for nome in notas:
    print(nome)
end
```

Literais tambem podem ser escritos como `{"Ada": 10, "Lin": 9.5}`. A leitura
de uma chave inexistente gera erro de runtime; a atribuicao insere ou atualiza
a chave. A iteracao visita as chaves em ordem crescente. Dicionarios aninhados,
valores compostos e chaves `number` nao sao suportados.
`dicionario.contains(chave)` devolve `bool`. `dicionario.remove(chave)` remove
a entrada e devolve `true` se ela existia ou `false` se nao existia; tambem pode
ser chamada como instrucao quando o resultado nao e necessario.
`dicionario.keys()` e `.values()` retornam listas na ordem das chaves do
dicionario. `dicionario.get(chave, padrao)` retorna o valor armazenado ou o
padrao quando a chave nao existe; o padrao precisa ter o tipo dos valores.
`dicionario.clear()` remove todas as entradas.

### Tuplas

Tuplas juntam valores de tipos escalares diferentes em uma estrutura fixa e
imutavel:

```sun
def rotulo(par: tuple[int, string]): tuple[string, int]
    return (par[1], par[0])
end

let pessoa: tuple[int, string] = (int(12), "Ada")
let invertida: tuple[string, int] = rotulo(pessoa)
print(pessoa)
print(pessoa[1])
print(invertida)
```

Uma tupla pode conter `string`, `number`, `bool`, `int` ou `char`. A anotacao
lista os tipos na ordem dos elementos; tuplas de um item usam a forma
`tuple[bool]` e literal `(true,)`. Os indices precisam ser literais inteiros
nao negativos, verificados durante a compilacao. Tuplas podem ser atribuidas,
passadas e retornadas, e comparadas com outra tupla do mesmo tipo. Atribuir a
um elemento, aninhar tuplas ou colocar tuplas em listas/dicionarios nao e
suportado.

Loops usam condicoes booleanas, iteracao por lista e iteracao numerica por
intervalo:

```sun
let i: number = 0
while i < 3:
    print(i)
    i = i + 1
end

for n in range(1, 4):
    print(n)
end
```

`while` repete enquanto a condicao for verdadeira. `range(inicio, fim)` inclui
o inicio e exclui o fim. Pode-se passar um terceiro argumento para o passo;
passos negativos contam para tras e o passo zero gera um erro em tempo de
execucao. Um passo que nao consegue avancar o valor atual tambem gera erro,
evitando loop infinito por precisao de ponto flutuante. Os valores de `range`
sao `number` (`double`), nao inteiros.

Funcoes podem declarar retorno com `: tipo` no cabecalho, usando tipos escalares
ou listas suportados (e `region` ou `ptr` com `import memory`). Sem anotacao, a
funcao retorna `void`. Todo caminho de uma funcao com retorno tipado deve
retornar um valor do tipo declarado:

```sun
def dobro(valor: number): number
    return valor * 2
end

let resultado: number = dobro(21)
```

Funcoes com retorno podem ser chamadas em expressoes. `return` sem valor so e
valido em funcoes sem tipo de retorno.

Parametros podem ter valores padrao no final da lista:

```sun
def cumprimentar(nome: string = "mundo", vezes: number = 1)
    for i in range(0, vezes):
        print(f"Ola, {nome}!")
    end
end

cumprimentar()
cumprimentar("Sun", 2)
```

Parametros com default precisam ser os ultimos. Os defaults aceitam expressoes
constantes do tipo declarado, mas nao podem depender de variaveis nem chamar
funcoes. Ao chamar a funcao, pode-se omitir qualquer sufixo de parametros que
tenha default.

Funcoes tambem podem ser declaradas dentro de outra funcao ou bloco. Elas so
ficam visiveis no escopo onde foram declaradas e podem capturar variaveis
visiveis do escopo externo por referencia, inclusive para altera-las:

```sun
def executar(): number
    let total: number = 0
    def adicionar(valor: number)
        total = total + valor
    end
    adicionar(3)
    adicionar(4)
    return total
end

print(executar())
```

Funcoes aninhadas aceitam parametros tipados, retorno tipado e parametros com
valores padrao constantes, inclusive recursao. Como nos defaults de funcoes de
nivel superior, os valores padrao nao podem chamar funcoes.

`istype(valor, tipo)` compara o tipo estatico de um valor com os tipos
escalares `string`, `number`, `bool`, `int` e `char`, ou com as categorias
`list`, `dict` e `tuple`. `region` e `ptr` tambem podem ser consultados quando
`import memory` esta ativo. As categorias reconhecem colecoes independentemente
dos tipos dos elementos ou das chaves:

```sun
let idade: number = 20
print(istype(idade, number))
print(istype(idade, string))
print(istype([1, 2], list))
print(istype({"Ada": 12}, dict))
print(istype(("Ada", int(12)), tuple))
```

Como Sun e estaticamente tipada, `istype` e decidido durante a compilacao e
gera `true` ou `false`; nao e uma verificacao dinamica de valores.

Dentro de `while` e `for`, `break` encerra o loop atual e `continue` avanca para
a proxima iteracao:

```sun
for n in range(0, 10):
    if n == 2:
        continue
    end
    if n == 5:
        break
    end
    print(n)
end
```

Essas instrucoes geram erro se forem usadas fora de um loop.

`import math` deve aparecer antes das declaracoes e instrucoes. Ele habilita as
funcoes `math.abs`, `math.acos`, `math.asin`, `math.atan`, `math.atan2`,
`math.ceil`, `math.cos`, `math.exp`, `math.factorial`, `math.floor`,
`math.fmod`, `math.hypot`, `math.log`, `math.log10`, `math.log2`, `math.pow`,
`math.sin`, `math.sqrt`, `math.tan` e `math.trunc`. As funcoes matematicas
recebem um ou dois argumentos numericos; todas, exceto `math.atan2`,
`math.fmod`, `math.hypot` e `math.pow`, recebem um.
`math.factorial(x)` (tambem disponivel como `math.fatorial(x)`) usa a
extensao gamma do fatorial, portanto aceita valores fracionarios e argumentos
maiores que 20, retornando `number`. Argumentos nao finitos, inteiros negativos
(polo da funcao gamma) e resultados fora da faixa de `number` geram erro em
runtime. As demais funcoes sao implementadas usando `<cmath>` do C++.

### Modulos locais

Um arquivo `.su` pode importar outro arquivo do projeto usando um caminho
relativo sem extensao e um alias. O modulo local e separado da biblioteca
padrao: `import math` continua escolhendo a stdlib, enquanto `import local/math`
carrega `local/math.su`.

Use `export def` para declarar a API publica de um modulo. Quando um modulo
usa pelo menos uma declaracao explicita de exportacao, somente as funcoes
marcadas ficam disponiveis ao importador; as demais podem ser usadas como
helpers internos. Para manter compatibilidade, modulos sem `export def`
continuam exportando todas as funcoes de nivel superior:

```sun
import math

export def distance(x: number, y: number): number
    return math.sqrt(x * x + y * y)
end

def clamp(value: number): number
    if value < 0:
        return 0
    end
    return value
end
```

O importador acessa `distance`, mas `clamp` permanece privado quando o modulo
declara exports explicitamente:

```sun
import local/geometry as geometry

print(geometry.distance(3, 4))
```

E possivel envolver o alias entre parenteses, mas isso e opcional. Modulos locais
podem importar outros modulos locais; ciclos sao reportados como erro. Por
enquanto, eles exportam funcoes e blocos `inline cpp` globais. Instrucoes
executaveis no nivel superior de um modulo nao sao aceitas.

## Biblioteca `sys`

`import sys` carrega o primeiro modulo da biblioteca padrao, escrito em
`stdlib/sys.su` e implementado com funcoes Sun e `inline cpp`:

```sun
import sys

print(sys.args())
print(sys.platform())
print(sys.cwd())
print(sys.env("HOME", ""))
print(sys.env_exists("HOME"))
sys.exit(int(0))
```

`sys.args()` retorna uma lista com os argumentos passados ao executavel,
excluindo o nome do executavel. `sys.platform()` retorna `windows`, `macos`,
`linux`, `unix` ou `unknown`. `sys.exit(code)` encerra o processo com um codigo
de saida que precisa caber no tipo `int` do sistema.
`sys.cwd()` retorna o diretorio de trabalho atual. `sys.env(name, fallback)`
retorna o valor de uma variavel de ambiente ou o texto alternativo informado;
`sys.env_exists(name)` informa se ela esta definida.

### Structs, enums e classes

`struct` declara campos e pode ser inicializada passando os valores na ordem
declarada. `enum` declara variantes nomeadas. `class` aceita campos, metodos e
um metodo opcional `init`; `self` refere-se ao objeto atual:

```sun
enum Color
    Red
    Blue
end

struct Point
    x: number
    y: number
end

class Counter
    value: int

    def init(start: int)
        self.value = start
    end

    def increment()
        self.value = self.value + int(1)
    end
end

let point: Point = Point(2, 3)
let color: Color = Color.Blue
let counter: Counter = Counter(int(4))
counter.increment()
print(point.x)
print(color)
print(counter.value)
```

Tipos customizados podem ser usados em parametros, retornos e variaveis.
Campos podem usar os tipos escalares ja suportados, mas nao outros tipos
customizados ou listas. Classes comuns aceitam heranca simples de uma classe
declarada anteriormente. Campos e metodos sao herdados, e metodos podem ser
sobrescritos com a mesma lista de tipos de parametros e o mesmo tipo de retorno.
Nao ha conversao implicita de uma classe derivada para sua classe base.

Se a classe base (ou algum ancestral) declarar `init`, a classe derivada herda
esse inicializador quando nao declara o seu proprio. Se declarar um inicializador
proprio, ele deve comecar com `super.init(...)` quando houver um inicializador
herdado; essa chamada deve ser a primeira instrucao executavel. Se nao houver
`init` na hierarquia, o construtor de uma classe derivada sem inicializador
aceita os campos herdados e proprios e os atribui diretamente. Se houver um
inicializador herdado, os campos proprios da derivada comecam com seus valores
padrao. Heranca multipla, classes genericas e listas de objetos ainda nao sao
suportadas. Classes de erro continuam herdando diretamente apenas de `AnError`.

```sun
class Animal
    name: string
    def init(name: string)
        self.name = name
    end
    def sound(): string
        return "?"
    end
end

class Dog(Animal)
    def init(name: string)
        super.init(name)
    end
    def sound(): string
        return "woof"
    end
end

let dog: Dog = Dog("Rex")
print(dog.name)
print(dog.sound())
```

Enums podem declarar variantes com campos escalares, e `match` seleciona um
bloco por valor ou variante. Os campos de uma variante podem ser ligados a nomes
no padrao; `_` ignora um campo. Casos repetidos sao rejeitados. Sem `else`,
um `match` de enum precisa cobrir todas as variantes e um `match` de `bool`
precisa cobrir `true` e `false`; para outros valores escalares, `else` e
opcional. Nao ha fallthrough:

```sun
enum Result
    Ok(value: number)
    Error(message: string)
    Pending
end

let result: Result = Result.Ok(42)
match result:
    case Result.Ok(value):
        print(value)
    case Result.Error(message):
        print(message)
    case Result.Pending:
        print("pending")
end

match 2:
    case 1:
        print("one")
    case 2:
        print("two")
    else:
        print("other")
end
```

Os tipos dos valores de cada payload devem ser escalares (`string`, `number`,
`bool`, `int` ou `char`). Padroes escalares precisam ter o mesmo tipo do valor
analisado; para `int` e `char`, escreva a conversao literal, como `int(1)` ou
`char("A")`. A variante enum pode ser impressa; a representacao mostra o nome
da variante.

O exemplo executavel completo esta em `examples/classes.su`:

```sh
build/sun examples/classes.su --compile
./examples/classes
```

## 7. Biblioteca `io`

`import io` habilita leitura e escrita de arquivos de texto. O caminho e uma
string e pode ser relativo ao diretorio de trabalho do processo:

```sun
import io

let path: string = "notes.txt"
io.write_text(path, "Primeira linha\n")
io.append_text(path, "Outra linha\n")

let contents: string = io.read_text(path)
print(contents)
print(io.exists(path))
```

`io.write_text(path, contents)` cria ou substitui o arquivo;
`io.append_text(path, contents)` acrescenta ao final; `io.read_text(path)`
retorna o conteudo completo como `string`; `io.exists(path)` verifica se o
caminho existe. `io.read_lines(path)` devolve as linhas como `list[string]`;
`io.is_directory(path)` verifica se o caminho e um diretorio;
`io.create_directories(path)` cria a arvore de diretorios e retorna `true` se
algo foi criado; `io.remove(path)` remove um arquivo ou diretorio vazio e
retorna se removeu algo; `io.file_size(path)` retorna o tamanho em bytes como
`int`.
Para dados binarios, `io.read_bytes(path)` retorna um valor `bytes` e
`io.write_bytes(path, dados)` grava os octetos sem conversao de texto.
`io.read_bytes` e `io.write_bytes` usam o caminho e nao alteram a semantica dos
handles `rb`, cuja leitura continua retornando `string`.
`io.createdir(path, exist="ok")` tambem cria diretorios e seus pais, retornando
`true` se criou o destino e `false` se ele ja era um diretorio. Com
`exist="no"`, a chamada gera erro se o destino ja existir; um caminho existente
que nao seja diretorio sempre gera erro. O argumento `exist` aceita somente
`"ok"` ou `"no"` e pode ser omitido para usar `"ok"`.
`open(path, mode)` (ou `io.open`) retorna um handle que pode ser usado com
`read(handle)`, `io.read(handle)`, `write(handle, contents)`/`io.write` e
`close(handle)`/`io.close`. Os modos suportados sao `r` (leitura), `w`
(escrita truncada), `a` (acrescenta ao final e cria o arquivo se necessario),
`rb` (leitura binaria, devolvida como `string`) e `pw` (atualizacao de uma
linha existente). `io.write` aceita handles abertos em `w` ou `a`:

```sun
import io

with open("notes.txt", "w") as file:
    io.write(file, "Primeira linha\nSegunda linha\n")
end

with open("notes.txt", "a") as file:
    io.write(file, "Linha acrescentada\n")
end

let file: int = open("notes.txt", "pw")
io.pw(file, int(2), "Linha atualizada")
io.close(file)

with open("notes.txt", "r") as file:
    print(io.read(file))
end
```

`with open(path, mode) as nome:` fecha o handle ao sair do bloco, inclusive
quando o corpo retorna ou gera um erro. `io.pw(handle, linha, conteudo)` troca
uma linha completa, usando numeracao iniciada em 1. O conteudo precisa ser uma
unica linha; linhas inexistentes geram erro.
Erros ao abrir, ler ou gravar arquivos geram erros de runtime Sun e podem ser
capturados com `try`/`catch`. A implementacao C++ isolada da biblioteca fica em
`src/stdlib_io.cpp`, e o runtime necessario e incluido no C++ gerado.

## 8. Biblioteca `json`

`import json` habilita `json.loads(texto)` e `json.dumps(valor)`, implementados
no runtime C++ sem dependencias externas. O tipo dinamico `json` preserva
objetos e arrays aninhados e permite leitura e atualizacao por indice:

```sun
import json

let data: json = json.loads("{\"user\":{\"name\":\"Ada\"},\"scores\":[10,9]}")
print(data["user"]["name"].as_string())
print(data["scores"][0].as_int())
data["user"]["name"] = "Grace"
print(json.dumps(data))
```

O acesso a objetos usa chaves `string`; arrays usam indices `int` ou `number`.
`.contains(chave)` verifica se um objeto tem uma chave sem lancar erro quando
ela falta. `.append(valor)` acrescenta um valor JSON-compativel ao final de um
array e retorna o novo tamanho; o valor pode ser um escalar, JSON, lista tipada
ou dicionario com chaves `string`. Ambos falham em runtime se usados no tipo de
container errado. Esses metodos exigem um valor JSON mutavel.
Conversoes de tipo verificadas incluem `.as_string()`, `.as_number()`,
`.as_int()` e `.as_bool()`; `.is_null()` testa `null`, e `.size()` retorna o
tamanho de objetos, arrays ou strings. `json.dumps` tambem serializa valores
escalares, listas tipadas e dicionarios com chaves `string`.
JSON malformado, indices fora dos limites ou conversoes incompatíveis geram
erros de runtime Sun. A implementacao fica em `src/stdlib_json.cpp`.
Inteiros JSON fora do intervalo de `int` permanecem preservados na
serializacao; `.as_int()` os rejeita, enquanto `.as_number()` os converte para
`number` com a precisao limitada de `double`. `json.dumps` nao aceita `NaN` ou
infinito.

## 9. Biblioteca `memory`

`import memory` habilita regioes de bytes gerenciadas e ponteiros opacos `ptr`.
As regioes continuam com acesso por bytes e limites verificados; `ptr` e um
equivalente generico a `void*` para alocacao e interoperabilidade com C++.
Cada alocacao e limitada a 64 MiB; offsets
precisam ser numeros inteiros dentro dos limites da regiao e os valores escritos
precisam estar entre 0 e 255:

```sun
import memory

let buffer: region = memory.alloc(4)
memory.write_u8(buffer, 0, 65)
let first_byte: number = memory.read_u8(buffer, 0)
print(first_byte)
print(memory.size(buffer))
```

As operacoes disponiveis sao `memory.alloc(tamanho)`, `memory.size(regiao)`,
`memory.read_u8(regiao, offset)`, `memory.read_u16(regiao, offset, endian)` e
`memory.read_u32(regiao, offset, endian)`, alem das instrucoes
`memory.write_u8(regiao, offset, valor)`,
`memory.write_u16(regiao, offset, valor, endian)`,
`memory.write_u32(regiao, offset, valor, endian)`,
`memory.fill(regiao, valor)` e
`memory.copy(destino, offset_destino, origem, offset_origem, comprimento)`.
`u16` e `u32` retornam `int`; as escritas aceitam valores de 0 a 65535 e de 0 a
4294967295, respectivamente. O argumento obrigatorio `endian` aceita `"little"`
ou `"big"`. `fill` preenche toda a regiao com o byte indicado. `copy` copia um
intervalo entre regioes e tambem funciona quando origem e destino se sobrepoem.
`alloc` aceita tamanhos inteiros de 0 a 67108864; regioes com tamanho zero nao
tem offsets legiveis ou gravaveis. Leitura e escrita fora dos limites, offsets
ou comprimentos fracionarios/negativos e valores invalidos geram erro em tempo
de execucao. Para uma copia de comprimento zero, os offsets podem apontar para
o final da regiao. `u64` ainda nao esta disponivel, pois a linguagem nao tem um
tipo inteiro sem sinal que represente toda a faixa de 64 bits sem perda.

`memory.alloc_ptr(tamanho)` aloca memoria sem associar um tipo de elemento e
retorna `ptr`. `memory.ptr_size(ptr)` informa o tamanho em bytes e
`memory.ptr_is_null(ptr)` verifica se o ponteiro e nulo. Use
`memory.free_ptr(ptr)` para liberar explicitamente a alocacao; liberar um
ponteiro nulo ou ja liberado nao faz nada. Copias de um `ptr` compartilham a
mesma alocacao, portanto libera-la invalida todas as copias. Se nenhuma copia
restar, a memoria tambem e liberada automaticamente.

Sun nao permite dereferenciar `ptr` nem converter enderecos arbitrarios. Para
interoperar com C ou C++, passe o ponteiro a um bloco `inline cpp` e use
`nome.get()` para obter um `void*` emprestado, valido enquanto a alocacao nao
for liberada. Nao libere esse endereco diretamente em C++; use
`memory.free_ptr`. A partir do acesso a `.get()`, o codigo C++ tem os
privilegios e riscos normais de acesso a ponteiros nativos.

Uma regiao atribuida a outra variavel ou passada para uma funcao referencia a
mesma alocacao. A memoria e liberada automaticamente quando nao houver mais
regioes referenciando-a. O tipo `region` nao pode ser comparado diretamente.
Sua representacao em texto e `region(size=N)`; ela mostra o tamanho atual,
inclusive `region(size=0)` para regioes vazias.

## 10. Tipo `bytes`

`bytes` e uma sequencia mutavel de octetos, indexada a partir de zero. Cada
valor deve estar entre 0 e 255; indices, atribuicoes e inclusoes sao verificados
em runtime:

```sun
import io

let data: bytes = bytes.from_list([int(65), int(66)])
data[1] = int(67)
data.append(10)
print(len(data))
print(data.contains(67))
io.write_bytes("message.bin", data)

let loaded: bytes = io.read_bytes("message.bin")
for value in loaded:
    print(value)
end
```

`bytes.from_string`/`bytes.to_string` copiam os octetos da string sem validar
UTF-8; `bytes.from_list`/`bytes.to_list` convertem entre `bytes` e `list[int]`.
`bytes.from_region`/`bytes.to_region` copiam dados e requerem `import memory`.
`append`, `clear`, indexacao, `len`, `contains` e iteracao estao disponiveis.
Impressao direta mostra a sequencia como uma lista de octetos, por exemplo
`[65, 66]`; use `bytes.to_string()` para recuperar o texto original.

## 11. C++ inline

Blocos C++ literais podem ser usados para interoperabilidade e funcionalidades
que ainda nao existem na linguagem. O terminador e `endcpp`:

> **Aviso:** o conteudo de `inline cpp` e compilado e executado como C++ nativo,
> com os mesmos privilegios do programa. Sun nao o isola, valida nem torna
> seguro. Use somente com codigo confiavel; esse recurso nao e uma sandbox.

```sun
inline cpp:
#include <iostream>
int helper_cpp() { return 42; }
endcpp

def mostrar(valor: number)
    let resposta: number = valor + 2
    inline cpp:
        std::cout << valor << " + 2 = " << resposta << '\n';
    endcpp
end

mostrar(40)
```

Um bloco no nivel superior e copiado para o escopo global do C++ gerado; use
esse local para `#include`, declaracoes e funcoes C++. Um bloco dentro de uma
funcao ou condicional e copiado para aquele ponto do corpo e pode ler ou alterar
os parametros e variaveis Sun visiveis ali, usando seus nomes originais.
Identificadores C++ reservados nao podem ser expostos como aliases.

O trecho inline precisa ser C++ valido e compativel com o compilador escolhido.
Ele pode acessar ou afetar qualquer estado disponivel ao programa, inclusive
arquivos e outros recursos do sistema operacional, sujeito aos privilegios do
processo.

## 12. Comentarios e regras atuais

Linhas vazias e comentarios iniciados por `#` sao ignorados. O comentario pode
aparecer depois de uma instrucao e nao altera `#` dentro de uma string:

```sun
# Este e um comentario
print("Ola!") # comentario no fim da linha
```

Dentro de `inline cpp`, as linhas sao copiadas literalmente; use os comentarios
normais de C++ (`//` ou `/* ... */`) nesse bloco.

Atualmente, Sun suporta declaracoes de funcao no nivel superior, parametros
tipados, variaveis locais tipadas, constantes, chamadas, `print`, strings,
numeros, `int`, `char`, listas homogeneas, dicionarios e tuplas, entrada tipada
com `input`, operadores aritmeticos, de comparacao e logicos, f-strings, `%v`,
`if`/`elif`/`else`, loops `while`, `for` por `range` e `for` por lista, retornos
tipados, `try`/`catch` para erros de runtime Sun, as bibliotecas `math`, `io`,
`json`, `memory` e `sys`, `with open`, `istype`, parametros com valores padrao,
imports de modulos locais, structs, enums com payload, `match`, classes com
heranca simples, funcoes aninhadas e C++ inline. Classes de erro podem herdar de
`AnError`.

## 12. Erros e acessibilidade

Erros de sintaxe e tipos aparecem imediatamente com arquivo, linha, coluna e
trecho destacado. Quando possivel, Sun tambem sugere como corrigir o problema:

```text
error: unknown function 'desconhecida'
 --> app.su:3:1
  |
3 | desconhecida()
  | ^^^^^^^^^^^^
  = help: Check the spelling or declare the function with 'def desconhecida(...)'.
```

As mensagens sao texto simples, sem depender de cores, e os erros vao para
`stderr`; isso permite ler a saida em terminais simples e encaminha-la para
ferramentas assistivas. As sugestoes de `help` aparecem junto com o diagnostico,
sem exigir uma opcao ou comando separado.

Use `try` com um ou mais `except Tipo [as nome]:` para tratar erros por tipo.
`as nome` e opcional e, quando informado, recebe a mensagem como `string`.
Os handlers sao testados na ordem em que aparecem; coloque os tipos mais
especificos antes de `AnError`:

```sun
try:
    let idade: int = input("Idade: ", int)
except AnError as mensagem:
    print(f"Erro de runtime: {mensagem}")
end
```

As operacoes de runtime do Sun (como conversoes, indices invalidos e erros de
entrada) sao capturadas por `AnError`. Erros fatais e excecoes arbitrarias de
C++ nao pertencem a essa hierarquia e nao sao capturados por `except AnError`.
`AnError` deve ser o ultimo handler, pois funciona como wildcard para os erros
Sun que ainda nao foram capturados por handlers anteriores. Os handlers
tipados aceitam `AnError` ou qualquer classe derivada dela.

Um bloco `finally:` opcional pode vir depois dos handlers, ou sozinho depois
do corpo `try`. Ele executa ao sair do bloco, inclusive quando ocorre `return`
ou uma excecao nao tratada:

```sun
try:
    let contents: string = io.read_text("config.txt")
except AnError as erro:
    print(erro)
finally:
    print("Operacao finalizada")
end
```

`finally` nao pode conter `return`, `break` ou `continue` que tentem escapar do
bloco. Se ele lancar uma excecao durante a propagacao de outra excecao, a
excecao original continua sendo propagada. Como em outras linguagens, ele nao
executa se o processo for encerrado diretamente por `sys.exit`, `abort`, falha
de memoria/segmentacao ou encerramento externo.

Para blocos `inline cpp` executados dentro de um `try`, use `except cppCatcher`
para capturar excecoes C++ lancadas pelo proprio bloco. `as nome` e opcional;
quando usado, a variavel recebe a mensagem da excecao:

```sun
try:
    inline cpp:
        throw std::runtime_error("falha nativa");
    endcpp
except cppCatcher as erro:
    print(erro)
except AnError:
    print("Outro erro Sun")
end
```

`cppCatcher` captura excecoes C++ padrao e excecoes lancadas com `throw` que
nao derivam de `std::exception` (estas recebem uma mensagem generica). Ele nao
captura falhas fatais do processo, como `abort`, falhas de memoria/segmentacao
ou encerramento via `exit`. Excecoes C++ fora de blocos `inline cpp` nao sao
convertidas para esse handler.

Erros personalizados herdam da classe-base `AnError` e sao lancados com
`raise`. O metodo `init`, quando declarado, precisa receber a mensagem como
primeiro argumento `string`; os demais parametros podem inicializar campos do
erro:

```sun
class ValidationError(AnError)
    code: int
    def init(message: string, code: int)
        self.code = code
    end
end

try:
    raise ValidationError("entrada invalida", int(12))
except ValidationError as mensagem:
    print(mensagem)
except AnError:
    print("Outro erro")
end
```

`raise` aceita somente um valor de `AnError` ou de uma classe que herde dela.
`catch nome:` continua disponivel para compatibilidade e captura erros de
runtime Sun, disponibilizando a mensagem como `string`.

Handlers `except` nao capturam erros de sintaxe/tipo da compilacao nem excecoes
arbitrarias de C++ inline.

Problemas comuns:

- **`source file must have the .su extension`**: confirme a extensao do arquivo.
- **`missing 'end'`**: adicione `end` para fechar cada funcao, condicional ou loop.
- **`missing 'endcpp'`**: feche cada bloco C++ com `endcpp`.
- **`range step cannot be zero`**: use um passo diferente de zero em `range`.
- **`unknown value`**: use um parametro declarado ou variavel local ja declarada.
- **`not enough %v values`**: adicione um argumento `%v=...` para cada marcador.
- **Erro ao compilar C++**: confirme se `g++`/`clang++` esta instalado ou informe
  o compilador com `--compiler`.

## 13. Executar os testes

Com Python 3 instalado:

```sh
make test
```

`make test` distribui os casos entre ate quatro processos, cada um com uma
pasta temporaria isolada. Ajuste o paralelismo quando necessario, por exemplo
`make test TEST_JOBS=2`; use `make test-serial` para executar a suite sequencial.
