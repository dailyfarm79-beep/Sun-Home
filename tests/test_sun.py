import os
import pathlib
import shutil
import subprocess
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
COMPILER = ROOT / "build" / "sun"
WORK = pathlib.Path(os.environ.get("SUN_TEST_WORK", ROOT / "build" / "test-sun"))


class SunCompilerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        WORK.mkdir(parents=True, exist_ok=True)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(WORK, ignore_errors=True)

    def invoke(self, source_name, source, *arguments):
        source_path = WORK / source_name
        source_path.write_text(source, encoding="utf-8")
        return subprocess.run(
            [str(COMPILER), str(source_path), *arguments],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )

    def test_help_option(self):
        result = subprocess.run(
            [str(COMPILER), "--help"],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0)
        self.assertIn("Usage: sun <source.su>", result.stdout)
        self.assertIn("--check", result.stdout)

    @unittest.skipUnless(shutil.which("clang++"), "clang++ is required for LLVM tests")
    def test_llvm_backend_compiles_numeric_variables_and_control_flow(self):
        result = self.invoke(
            "llvm-basic.su",
            "let count: int = int(3)\n"
            "count = count + int(4)\n"
            "print(count)\n"
            "let amount: number = 1.5 + 2.5\n"
            "print(amount)\n"
            "let ok: bool = count > int(4) and true\n"
            "print(ok)\n"
            "if ok:\n"
            '    print("branch taken")\n'
            "else:\n"
            '    print("branch skipped")\n'
            "end\n"
            "while count < int(9):\n"
            "    count = count + int(1)\n"
            "end\n"
            "print(count)\n"
            "let steps: int = int(0)\n"
            "while steps < int(4):\n"
            "    steps = steps + int(1)\n"
            "    if steps == int(2):\n"
            "        continue\n"
            "    end\n"
            "    if steps == int(3):\n"
            "        break\n"
            "    end\n"
            "    print(steps)\n"
            "end\n"
            'let message: string = "A\\0B"\n'
            "print(message)\n"
            'message = "done"\n'
            "print(message)\n",
            "--llvm",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "llvm-basic")],
            cwd=ROOT,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            b"7\n4\ntrue\nbranch taken\n9\n1\nA\x00B\ndone\n",
        )

    @unittest.skipUnless(shutil.which("clang++"), "clang++ is required for LLVM tests")
    def test_llvm_backend_compiles_functions_and_string_parameters(self):
        result = self.invoke(
            "llvm-functions.su",
            "def add(left: int, right: int): int\n"
            "    return left + right\n"
            "end\n"
            "def greet(name: string)\n"
            "    print(name)\n"
            "end\n"
            "def echo(name: string): string\n"
            "    return name\n"
            "end\n"
            "print(add(int(2), int(5)))\n"
            'greet("hello")\n'
            'print(echo("world"))\n',
            "--emit-llvm",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            ["clang++", "-x", "ir", str(WORK / "llvm-functions.ll"), "-o",
             str(WORK / "llvm-functions")],
            cwd=ROOT,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        output = subprocess.run(
            [str(WORK / "llvm-functions")],
            cwd=ROOT,
            capture_output=True,
            check=False,
        )
        self.assertEqual(output.returncode, 0, output.stderr)
        self.assertEqual(output.stdout, b"7\nhello\nworld\n")

    @unittest.skipUnless(shutil.which("clang++"), "clang++ is required for LLVM tests")
    def test_llvm_backend_compiles_range_and_short_circuit(self):
        result = self.invoke(
            "llvm-range.su",
            "def boom(): bool\n"
            "    return int(1) / int(0) == int(0)\n"
            "end\n"
            "let limit: int = int(3)\n"
            "for i in range(1, 4):\n"
            "    print(i)\n"
            "end\n"
            "for i in range(3, 0, -1):\n"
            "    print(i)\n"
            "end\n"
            "print(false and boom())\n"
            "print(true or boom())\n",
            "--llvm",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        output = subprocess.run(
            [str(WORK / "llvm-range")],
            cwd=ROOT,
            capture_output=True,
            check=False,
        )
        self.assertEqual(output.returncode, 0, output.stderr)
        self.assertEqual(output.stdout, b"1\n2\n3\n3\n2\n1\nfalse\ntrue\n")

    @unittest.skipUnless(shutil.which("clang++"), "clang++ is required for LLVM tests")
    def test_llvm_backend_compiles_scalar_fstrings(self):
        result = self.invoke(
            "llvm-fstring.su",
            'let count: int = int(7)\n'
            'let amount: number = 1.5\n'
            'let ok: bool = true\n'
            'let symbol: char = char("🌞")\n'
            'print(f"count={count} amount={amount} ok={ok} symbol={symbol}")\n',
            "--llvm",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        output = subprocess.run(
            [str(WORK / "llvm-fstring")],
            cwd=ROOT,
            capture_output=True,
            check=False,
        )
        self.assertEqual(output.returncode, 0, output.stderr)
        self.assertEqual(
            output.stdout,
            "count=7 amount=1.5 ok=true symbol=🌞\n".encode(),
        )

    @unittest.skipUnless(shutil.which("clang++"), "clang++ is required for LLVM tests")
    def test_llvm_backend_compiles_list_mutation_and_iteration(self):
        result = self.invoke(
            "llvm-list.su",
            "let values: list[int] = [int(1), int(2), int(2)]\n"
            "print(values.contains(int(2)))\n"
            "values.insert(int(1), int(9))\n"
            "values[int(0)] = int(7)\n"
            "let removed: bool = values.remove(int(2))\n"
            "print(removed)\n"
            "let popped: int = values.pop(int(1))\n"
            "print(popped)\n"
            "for value in values:\n"
            "    print(value)\n"
            "end\n"
            "values.clear()\n"
            "print(len(values))\n",
            "--llvm",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        output = subprocess.run(
            [str(WORK / "llvm-list")],
            cwd=ROOT,
            capture_output=True,
            check=False,
        )
        self.assertEqual(output.returncode, 0, output.stderr)
        self.assertEqual(output.stdout, b"true\ntrue\n9\n7\n2\n0\n")

    @unittest.skipUnless(shutil.which("clang++"), "clang++ is required for LLVM tests")
    def test_llvm_backend_compiles_dictionary_operations_and_iteration(self):
        result = self.invoke(
            "llvm-dict.su",
            'let ages: dict[string, int] = {"Ada": int(37), "Lin": int(31)}\n'
            "print(ages)\n"
            'print(ages.get("Ada", int(0)))\n'
            'print(ages.get("Grace", int(0)))\n'
            "print(ages.keys())\n"
            "print(ages.values())\n"
            "for name in ages:\n"
            "    print(name)\n"
            "end\n"
            "let ranks: dict[int, int] = {int(10): int(1), int(2): int(2)}\n"
            "for rank in ranks:\n"
            "    print(rank)\n"
            "end\n"
            "let flags: dict[bool, string] = {false: \"off\", true: \"on\"}\n"
            "print(flags)\n"
            "print(flags[true])\n"
            "print(flags.get(false, \"fallback\"))\n"
            "print(flags.keys())\n"
            "for flag in flags:\n"
            "    print(flag)\n"
            "end\n",
            "--llvm",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        output = subprocess.run(
            [str(WORK / "llvm-dict")],
            cwd=ROOT,
            capture_output=True,
            check=False,
        )
        self.assertEqual(output.returncode, 0, output.stderr)
        self.assertEqual(
            output.stdout,
            b"{Ada: 37, Lin: 31}\n37\n0\n[Ada, Lin]\n[37, 31]\nAda\nLin\n2\n10\n{false: off, true: on}\non\noff\n[false, true]\nfalse\ntrue\n",
        )

    @unittest.skipUnless(shutil.which("clang++"), "clang++ is required for LLVM tests")
    def test_llvm_backend_compiles_bytes_conversions_and_mutation(self):
        result = self.invoke(
            "llvm-bytes.su",
            'let raw: bytes = bytes.from_string("ABC")\n'
            "raw[int(1)] = int(120)\n"
            "raw.append(int(33))\n"
            "print(raw.contains(int(120)))\n"
            "let values: list[int] = bytes.to_list(raw)\n"
            "print(values)\n"
            "let copied: bytes = bytes.from_list(values)\n"
            "print(bytes.to_string(copied))\n"
            "print(copied)\n"
            "let empty: bytes = bytes.from_list([])\n"
            "print(len(empty))\n"
            "for byte in raw:\n"
            "    print(byte)\n"
            "end\n",
            "--llvm",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        output = subprocess.run(
            [str(WORK / "llvm-bytes")],
            cwd=ROOT,
            capture_output=True,
            check=False,
        )
        self.assertEqual(output.returncode, 0, output.stderr)
        self.assertEqual(output.stdout, b"true\n[65, 120, 67, 33]\nAxC!\nAxC!\n0\n65\n120\n67\n33\n")

    @unittest.skipUnless(shutil.which("clang++"), "clang++ is required for LLVM tests")
    def test_llvm_backend_compiles_printc(self):
        result = self.invoke(
            "llvm-printc.su",
            'printc("Sol ☀", "red")\n',
            "--llvm",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        output = subprocess.run(
            [str(WORK / "llvm-printc")],
            cwd=ROOT,
            capture_output=True,
            check=False,
        )
        self.assertEqual(output.returncode, 0, output.stderr)
        self.assertEqual(output.stdout, "\x1b[31mSol ☀\x1b[0m\n".encode())

    @unittest.skipUnless(shutil.which("clang++"), "clang++ is required for LLVM tests")
    def test_llvm_backend_compiles_unicode_char_values(self):
        result = self.invoke(
            "llvm-char.su",
            'let source: string = "🌞"\n'
            "let symbol: char = char(source)\n"
            "def echo(value: char): char\n"
            "    return value\n"
            "end\n"
            "print(symbol)\n"
            "print(echo(symbol))\n"
            'print(char("é") == char("é"))\n'
            'print(char("A") != char("B"))\n',
            "--llvm",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        output = subprocess.run(
            [str(WORK / "llvm-char")],
            cwd=ROOT,
            capture_output=True,
            check=False,
        )
        self.assertEqual(output.returncode, 0, output.stderr)
        self.assertEqual(output.stdout, "🌞\n🌞\ntrue\ntrue\n".encode())

    def test_check_validates_without_creating_output(self):
        result = self.invoke(
            "check-only.su",
            'let count: int = int(3)\nprint(count)\n',
            "--check",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "")
        self.assertEqual(result.stderr, "")
        self.assertFalse((WORK / "check-only.cpp").exists())

    def test_check_reports_errors_with_source_context(self):
        result = self.invoke("check-error.su", 'print("ok")\nmissing()\n', "--check")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("error: unknown function 'missing'", result.stderr)
        self.assertIn("check-error.su:2:1", result.stderr)
        self.assertIn("  2 | missing()", result.stderr)
        self.assertIn("    | ^^^^^^^", result.stderr)
        self.assertIn("= help: Check the spelling or declare the function", result.stderr)
        self.assertFalse((WORK / "check-error.cpp").exists())

    def test_old_func_keyword_explains_its_replacement(self):
        result = self.invoke("old-function-keyword.su", "func greet()\nend\n", "--check")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("keyword 'func' was renamed to 'def'", result.stderr)
        self.assertIn("= help: Replace 'func' with 'def'", result.stderr)

    def test_check_explains_unknown_values_at_their_column(self):
        result = self.invoke("unknown-value.su", "print(missing)\n", "--check")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unknown-value.su:1:7", result.stderr)
        self.assertIn("= help: Declare the value with 'let'", result.stderr)

    def test_check_rejects_output_and_compilation_options(self):
        ignored_output = WORK / "ignored.cpp"
        for arguments in (("--compile",), ("-o", str(ignored_output)), ("--compiler", "g++")):
            with self.subTest(arguments=arguments):
                result = self.invoke("check-options.su", 'print("ok")\n',
                                     "--check", *arguments)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("--check", result.stderr)
                self.assertFalse((WORK / "check-options.cpp").exists())
                self.assertFalse(ignored_output.exists())

    def test_trailing_comments_preserve_strings_and_inline_cpp(self):
        result = self.invoke(
            "trailing-comments.su",
            "def show()\n"
            '    print("Sun # language comment") # ignored after expression\n'
            "    inline cpp:\n"
            '        std::cout << "C++ comment follows" << \'\\n\'; // # remains C++ text\n'
            "    endcpp\n"
            "end\n"
            "show()\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "trailing-comments")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "Sun # language comment\nC++ comment follows\n")

    def test_typed_dictionaries_support_lookup_update_length_and_iteration(self):
        result = self.invoke(
            "dictionaries.su",
            "let scores: dict[string, number] = {}\n"
            'scores["Ada"] = 10\n'
            'scores["Bo"] = 20\n'
            'scores["Ada"] = 15\n'
            'print(scores["Ada"])\n'
            'print(scores.contains("Ada"))\n'
            "print(len(scores))\n"
            "print(scores)\n"
            "for key in scores:\n"
            "    print(key)\n"
            "end\n"
            "try:\n"
            '    print(scores["missing"])\n'
            "catch error:\n"
            '    print(error)\n'
            "end\n"
            'let removed: bool = scores.remove("Bo")\n'
            "print(removed)\n"
            'print(scores.contains("Bo"))\n'
            'scores.remove("missing")\n'
            "print(len(scores))\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "dictionaries")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            "15\ntrue\n2\n{Ada: 15, Bo: 20}\nAda\nBo\n"
            "dictionary key not found\ntrue\nfalse\n1\n",
        )

    def test_dictionary_keys_values_get_and_clear(self):
        result = self.invoke(
            "dictionary-operations.su",
            'let ages: dict[string, int] = {"Ada": int(37), "Lin": int(31)}\n'
            "print(ages.keys())\n"
            "print(ages.values())\n"
            'print(ages.get("Ada", int(0)))\n'
            'print(ages.get("Grace", int(0)))\n'
            "ages.clear()\n"
            "print(len(ages))\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "dictionary-operations")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "[Ada, Lin]\n[37, 31]\n37\n0\n0\n")

    def test_additional_list_operations(self):
        result = self.invoke(
            "list-operations.su",
            "let values: list[int] = [int(1), int(2), int(2)]\n"
            "print(values.contains(int(2)))\n"
            "values.insert(int(1), int(9))\n"
            "print(values)\n"
            "let removed: bool = values.remove(int(2))\n"
            "print(removed)\n"
            "let popped: int = values.pop(int(1))\n"
            "print(popped)\n"
            "print(values)\n"
            "print(values.remove(int(9)))\n"
            "values.insert(int(2), int(3))\n"
            "print(values)\n"
            "values.clear()\n"
            "print(len(values))\n"
            "try:\n"
            "    values.pop(int(0))\n"
            "catch error:\n"
            "    print(error)\n"
            "end\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "list-operations")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            "true\n[1, 9, 2, 2]\ntrue\n9\n[1, 2]\nfalse\n"
            "[1, 2, 3]\n0\n"
            "list pop index out of bounds or not a non-negative integer\n",
        )

    def test_tuples_are_typed_immutable_and_indexed_at_compile_time(self):
        result = self.invoke(
            "tuples.su",
            "def swap(pair: tuple[int, string]): tuple[string, int]\n"
            "    return (pair[1], pair[0])\n"
            "end\n"
            'let pair: tuple[int, string] = (int(42), "Ada")\n'
            "let swapped: tuple[string, int] = swap(pair)\n"
            "print(pair)\n"
            "print(pair[0])\n"
            "print(pair[1])\n"
            "print(swapped)\n"
            "let singleton: tuple[bool] = (true,)\n"
            "print(singleton)\n"
            'print(pair == (int(42), "Ada"))\n',
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "tuples")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            "(42, Ada)\n42\nAda\n(Ada, 42)\n(true,)\ntrue\n",
        )

        invalid_index = self.invoke(
            "dynamic-tuple-index.su",
            'let pair: tuple[int, string] = (int(42), "Ada")\n'
            "let index: int = int(0)\n"
            "print(pair[index])\n",
            "--check",
        )
        self.assertNotEqual(invalid_index.returncode, 0)
        self.assertIn("tuple index must be a non-negative integer literal",
                      invalid_index.stderr)

    def test_istype_recognizes_collection_categories(self):
        result = self.invoke(
            "collection-istype.su",
            'print(istype((1, 2), tuple))\n'
            'print(istype(("one", true), tuple))\n'
            'print(istype(1, tuple))\n'
            'print(istype([1, 2], list))\n'
            'print(istype([], list))\n'
            'print(istype(1, list))\n'
            'print(istype({"one": 1}, dict))\n'
            'print(istype({}, dict))\n'
            'print(istype((1, 2), dict))\n'
            'print(istype(1, dict))\n',
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "collection-istype")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            "true\ntrue\nfalse\ntrue\ntrue\nfalse\ntrue\ntrue\nfalse\nfalse\n",
        )

    def test_transpiles_and_compiles_function_calls_and_strings(self):
        result = self.invoke(
            "greeting.su",
            'greet("Sun")\n'
            '\n'
            'def greet(name)\n'
            '    print("Hello, " + name + "!\\n")\n'
            'end\n',
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        generated = (WORK / "greeting.cpp").read_text(encoding="utf-8")
        self.assertIn("void sun_fn_greet(std::string sun_arg_name)", generated)
        self.assertIn('std::string("Hello, ", 7)', generated)
        self.assertIn("sun_arg_name", generated)

        executable = subprocess.run(
            [str(WORK / "greeting")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "Hello, Sun!\n\n")

    def test_named_arguments_work_for_functions_methods_and_constructors(self):
        result = self.invoke(
            "named-arguments.su",
            "def describe(name: string, age: int = int(0)): string\n"
            '    return f"{name}:{age}"\n'
            "end\n"
            "class Counter\n"
            "    value: int\n"
            "    def init(start: int)\n"
            "        self.value = start\n"
            "    end\n"
            "    def add(amount: int)\n"
            "        self.value = self.value + amount\n"
            "    end\n"
            "    def current(): int\n"
            "        return self.value\n"
            "    end\n"
            "    def plus(amount: int): int\n"
            "        return self.value + amount\n"
            "    end\n"
            "end\n"
            "struct Point\n"
            "    x: int\n"
            "    y: int\n"
            "end\n"
            'print(describe(age=int(21), name="Ada"))\n'
            'print(describe("Lin"))\n'
            "let counter: Counter = Counter(start=int(3))\n"
            "counter.add(amount=int(4))\n"
            "print(counter.current())\n"
            "print(counter.plus(amount=int(2)))\n"
            "let point: Point = Point(y=int(8), x=int(5))\n"
            "print(point.x + point.y)\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "named-arguments")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "Ada:21\nLin:0\n7\n9\n13\n")

    def test_named_argument_validation(self):
        cases = [
            (
                "unknown-named-argument",
                "def greet(name: string)\n"
                "    print(name)\n"
                "end\n"
                'greet(user="Ada")\n',
                "unknown named argument 'user'",
            ),
            (
                "duplicate-named-argument",
                "def greet(name: string)\n"
                "    print(name)\n"
                "end\n"
                'greet("Ada", name="Lin")\n',
                "multiple values for parameter 'name'",
            ),
            (
                "positional-after-named",
                "def combine(first: int, second: int)\n"
                "    print(first + second)\n"
                "end\n"
                "combine(first=int(1), int(2))\n",
                "positional arguments cannot follow named arguments",
            ),
        ]
        for source_name, source, expected_error in cases:
            with self.subTest(source=source_name):
                result = self.invoke(source_name + ".su", source, "--check")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(expected_error, result.stderr)

    def test_variadic_parameters_accept_typed_collection_unpacking(self):
        result = self.invoke(
            "variadic-unpacking.su",
            "def collect(prefix: int, *values: int)\n"
            "    print(prefix)\n"
            "    print(values)\n"
            "end\n"
            "def options(**values: string)\n"
            "    print(values)\n"
            "end\n"
            "let numbers: list[int] = [int(2), int(3)]\n"
            'let settings: dict[string, string] = {"mode": "fast", "label": "ok"}\n'
            "collect(int(1), *numbers)\n"
            "options(**settings)\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "variadic-unpacking")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "1\n[2, 3]\n{label: ok, mode: fast}\n")

    def test_variadic_unpacking_rejects_mismatched_collections_and_signatures(self):
        cases = [
            (
                "wrong-positional-item-type",
                "def collect(*values: int)\nend\n"
                'let words: list[string] = ["one"]\n'
                "collect(*words)\n",
                "unpacked list item type must be int",
            ),
            (
                "keyword-unpack-with-regular-parameters",
                "def collect(required: int, **values: string)\nend\n"
                'let options: dict[string, string] = {"mode": "fast"}\n'
                "collect(**options)\n",
                "**kwargs unpacking requires a function with only **kwargs",
            ),
            (
                "keyword-unpack-wrong-value-type",
                "def collect(**values: string)\nend\n"
                'let options: dict[string, int] = {"count": int(3)}\n'
                "collect(**options)\n",
                "**kwargs requires dict[string, string]",
            ),
        ]
        for source_name, source, expected_error in cases:
            with self.subTest(source=source_name):
                result = self.invoke(source_name + ".su", source, "--check")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(expected_error, result.stderr)

    def test_nested_function_parameters_accept_constant_defaults(self):
        result = self.invoke(
            "nested-defaults.su",
            "def outer(base: int): int\n"
            "    def add(value: int = int(4)): int\n"
            "        return base + value\n"
            "    end\n"
            "    print(add())\n"
            "    return add(value=int(3))\n"
            "end\n"
            "print(outer(int(2)))\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "nested-defaults")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "6\n5\n")

    def test_typed_variadic_arguments_collect_positional_and_named_values(self):
        result = self.invoke(
            "variadic-arguments.su",
            "def inspect(*values: int, **options: string)\n"
            "    print(values)\n"
            "    print(options)\n"
            "end\n"
            'inspect(int(2), int(3), label="items", mode="fast")\n'
            "def local_test(): int\n"
            "    def total(*values: int): int\n"
            "        let result: int = int(0)\n"
            "        for value in values:\n"
            "            result = result + value\n"
            "        end\n"
            "        return result\n"
            "    end\n"
            "    return total(int(4), int(5), int(6))\n"
            "end\n"
            "print(local_test())\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "variadic-arguments")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            "[2, 3]\n{label: items, mode: fast}\n15\n",
        )

    def test_variadic_parameters_require_valid_types_and_order(self):
        cases = [
            ("missing-variadic-type", "def collect(*values)\nend\n",
             "variadic parameters require an explicit scalar type"),
            ("regular-after-variadic",
             "def collect(*values: int, other: int)\nend\n",
             "variadic parameters must follow all regular parameters"),
            ("duplicate-variadic",
             "def collect(*values: int, *others: int)\nend\n",
             "a function may declare only one *args parameter"),
        ]
        for source_name, source, expected_error in cases:
            with self.subTest(source=source_name):
                result = self.invoke(source_name + ".su", source, "--check")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(expected_error, result.stderr)

    def test_string_operations_cover_transform_search_replace_and_split(self):
        result = self.invoke(
            "string-operations.su",
            'let text: string = "  Sun language  "\n'
            'let clean: string = text.trim()\n'
            'print("Hello".upper())\n'
            'print(clean.upper())\n'
            'print(clean.lower())\n'
            'print(clean.replace("Sun", "Moon"))\n'
            'print(clean.split(" "))\n'
            'print(clean.contains("language"))\n'
            'print(clean.starts_with("Sun"))\n'
            'print(clean.ends_with("language"))\n',
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "string-operations")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            "HELLO\nSUN LANGUAGE\nsun language\nMoon language\n[Sun, language]\n"
            "true\ntrue\ntrue\n",
        )

    def test_string_count_counts_non_overlapping_matches_and_rejects_empty_search(self):
        result = self.invoke(
            "string-count.su",
            'print("aaaa".count("aa"))\n'
            'print("banana".count("na"))\n'
            'print("banana".count("xyz"))\n'
            "try:\n"
            '    print("abc".count(""))\n'
            "catch error:\n"
            "    print(error)\n"
            "end\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "string-count")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            "2\n2\n0\nstring.count search text cannot be empty\n",
        )

    def test_string_split_and_replace_reject_empty_search_values_at_runtime(self):
        for operation, expression, expected_error in [
            ("split", '"a".split("")', "string.split separator cannot be empty"),
            ("replace", '"a".replace("", "b")',
             "string.replace search text cannot be empty"),
        ]:
            with self.subTest(operation=operation):
                result = self.invoke(
                    f"string-{operation}-empty.su",
                    "try:\n"
                    f"    print({expression})\n"
                    "except AnError as error:\n"
                    "    print(error)\n"
                    "end\n",
                    "--compile",
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                executable = subprocess.run(
                    [str(WORK / f"string-{operation}-empty")],
                    cwd=ROOT,
                    text=True,
                    capture_output=True,
                    check=False,
                )
                self.assertEqual(executable.returncode, 0, executable.stderr)
                self.assertEqual(executable.stdout, expected_error + "\n")

    def test_f_strings_interpolate_parameters_and_escape_braces(self):
        result = self.invoke(
            "f-string.su",
            "def double(value: number): number\n"
            "    return value * 2\n"
            "end\n"
            'def greet(name)\n'
            "    let count: int = int(3)\n"
            "    let items: list[int] = [int(7)]\n"
            '    print(f"Hello, {name}! {{welcome}}" + " again")\n'
            '    print(f"count={count + int(1)}, double={double(2.5)}, item={items[0]}")\n'
            '    print(f"brace={char(\\"}\\")}")\n'
            'end\n'
            'const constant_label: string = f"answer={int(3) + int(1)}"\n'
            'greet("Sun")\n'
            'print(constant_label)\n',
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "f-string")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            "Hello, Sun! {welcome} again\ncount=4, double=5, item=7\n"
            "brace=}\nanswer=4\n",
        )

    def test_f_string_reports_errors_in_embedded_expressions(self):
        result = self.invoke(
            "f-string-expression-error.su",
            'print(f"value={missing + 1}")\n',
            "--check",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unknown value 'missing'", result.stderr)

    def test_formatted_print_substitutes_values_and_escaped_percent(self):
        result = self.invoke(
            "formatted-print.su",
            'def greet(name)\n'
            '    print("Seu nome é %v (100%% Sun).", %v=name)\n'
            '    print("%v and %v.", %v=name, %v="Sun")\n'
            'end\n'
            'greet("Sol")\n',
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "formatted-print")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            "Seu nome é Sol (100% Sun).\nSol and Sun.\n",
        )

    def test_formatted_print_reports_missing_values(self):
        result = self.invoke("missing-format-value.su", 'print("Hello, %v")\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not enough %v values", result.stderr)

    def test_empty_print_has_a_source_error(self):
        result = self.invoke("empty-print.su", "print()\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("error: expected print(expression)", result.stderr)

    def test_printc_supports_named_ansi_rgb_and_formatted_text(self):
        result = self.invoke(
            "printc.su",
            'let name: string = "Sun"\n'
            'printc("red", "red")\n'
            'printc(f"Hello {name}", "blue", "rgb")\n'
            'printc("yellow", "yellow")\n'
            'printc("magenta rgb", "magenta", "rgb")\n'
            'printc("cyan", "cyan")\n'
            'printc("rgb", "12,34,56")\n'
            'printc("hex", "#0A1B2C")\n'
            'printc("ansi", "1;35")\n'
            'printc("escaped ansi", "\\\\033[36m")\n',
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "printc")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            "\x1b[31mred\x1b[0m\n"
            "\x1b[38;2;0;0;255mHello Sun\x1b[0m\n"
            "\x1b[33myellow\x1b[0m\n"
            "\x1b[38;2;255;0;255mmagenta rgb\x1b[0m\n"
            "\x1b[36mcyan\x1b[0m\n"
            "\x1b[38;2;12;34;56mrgb\x1b[0m\n"
            "\x1b[38;2;10;27;44mhex\x1b[0m\n"
            "\x1b[1;35mansi\x1b[0m\n"
            "\x1b[36mescaped ansi\x1b[0m\n",
        )

    def test_printc_rejects_invalid_colors_and_arguments(self):
        cases = (
            ('printc(1, "red")\n', "printc text must be a string or f-string"),
            ('let color: string = "red"\nprintc("x", color)\n',
             "printc color must be a quoted string literal"),
            ('printc("x", "orange")\n', "ANSI color code must contain numeric SGR values"),
            ('printc("x", "red", "blue")\n', "printc format must be 'ansi' or 'rgb'"),
            ('printc("x", "256,0,0")\n', "RGB components must be integers between 0 and 255"),
            ('printc("x", "31", "ansi")\n', "printc format applies only to named colors"),
            ('printc("x")\n', "expected printc(string, color[, format])"),
        )
        for index, (source, message) in enumerate(cases):
            with self.subTest(source=source):
                result = self.invoke(f"printc-error-{index}.su", source, "--check")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(message, result.stderr)

    def test_numeric_operators_math_and_if_elif_else(self):
        result = self.invoke(
            "math-branches.su",
            "import math\n"
            "def classify(x: number)\n"
            "    let magnitude: number = math.sqrt(math.pow(x, 2))\n"
            "    let score: number = magnitude + 2 * 3\n"
            "    if score < 7:\n"
            '        print("small")\n'
            "    elif score == 7:\n"
            '        print("seven")\n'
            "    else:\n"
            '        print(f"large {score}")\n'
            "    end\n"
            "end\n"
            "classify(1)\n"
            "classify(2)\n"
            "classify(0)\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "math-branches")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "seven\nlarge 8\nsmall\n")

    def test_math_additional_trigonometric_logarithmic_and_numeric_functions(self):
        result = self.invoke(
            "math-additions.su",
            "import math\n"
            "print(math.acos(1))\n"
            "print(math.asin(0))\n"
            "print(math.atan(0))\n"
            "print(math.atan2(0, 1))\n"
            "print(math.fmod(7, 4))\n"
            "print(math.hypot(3, 4))\n"
            "print(math.log10(100))\n"
            "print(math.log2(8))\n"
            "print(math.trunc(2.9))\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "math-additions")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "0\n0\n0\n0\n3\n5\n2\n3\n2\n")

    def test_math_factorial_accepts_general_numbers_and_reports_undefined_results(self):
        result = self.invoke(
            "math-factorial.su",
            "import math\n"
            "print(math.factorial(0))\n"
            "print(math.factorial(1))\n"
            "print(math.factorial(5))\n"
            "print(math.factorial(10))\n"
            "let fact36: number = math.factorial(36)\n"
            "print(fact36 / math.factorial(35))\n"
            "print(math.factorial(2.5) > 3.3)\n"
            "print(math.fatorial(36) / math.fatorial(35))\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "math-factorial")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            "1\n1\n120\n3.6288e+06\n36\ntrue\n36\n",
        )

        for value in ("-1", "171", "1e308"):
            with self.subTest(value=value):
                invalid = self.invoke(
                    "math-factorial-invalid.su",
                    "import math\n"
                    f"print(math.factorial({value}))\n",
                    "--compile",
                )
                self.assertEqual(invalid.returncode, 0, invalid.stderr)
                failed = subprocess.run(
                    [str(WORK / "math-factorial-invalid")],
                    cwd=ROOT,
                    text=True,
                    capture_output=True,
                    check=False,
                )
                self.assertNotEqual(failed.returncode, 0)
                self.assertIn(
                    "math.factorial is undefined at negative integers or outside the number range",
                    failed.stderr,
                )

    def test_constant_expressions_are_folded_in_generated_cpp(self):
        result = self.invoke(
            "constant-folding.su",
            "import math\n"
            "print(int(2) + int(3) * int(4))\n"
            "print(7 < 9)\n"
            "print(math.sqrt(81))\n"
            "print(1.0 / 3.0)\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        generated = (WORK / "constant-folding.cpp").read_text(encoding="utf-8")
        self.assertIn("sun_to_string(std::int64_t{14})", generated)
        self.assertIn("sun_to_string(true)", generated)
        self.assertIn("sun_to_string(9.0)", generated)
        main_body = generated.split("int main()", 1)[1]
        self.assertNotIn("sun_int_mul", main_body)
        executable = subprocess.run(
            [str(WORK / "constant-folding")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "14\ntrue\n9\n0.333333\n")

    def test_const_values_are_immutable_and_folded(self):
        result = self.invoke(
            "const-values.su",
            "const base: int = int(20)\n"
            "const answer: int = base + int(22)\n"
            'const greeting: string = "Sun"\n'
            'const symbol: char = char("☀")\n'
            "print(answer)\n"
            "print(greeting)\n"
            "print(symbol)\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        generated = (WORK / "const-values.cpp").read_text(encoding="utf-8")
        main_body = generated.split("int main()", 1)[1]
        self.assertIn("sun_to_string(std::int64_t{42})", main_body)
        executable = subprocess.run(
            [str(WORK / "const-values")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "42\nSun\n☀\n")

    def test_const_rejects_mutation_and_nonconstant_initializers(self):
        reassignment = self.invoke(
            "const-reassignment.su",
            "const limit: int = int(4)\n"
            "limit = int(5)\n",
            "--check",
        )
        self.assertNotEqual(reassignment.returncode, 0)
        self.assertIn("cannot assign to constant 'limit'", reassignment.stderr)

        nonconstant = self.invoke(
            "const-variable.su",
            "let value: int = int(2)\n"
            "const result: int = value\n",
            "--check",
        )
        self.assertNotEqual(nonconstant.returncode, 0)
        self.assertIn("constant initializer must be a compile-time constant expression",
                      nonconstant.stderr)

        failing = self.invoke(
            "const-division-by-zero.su",
            "const value: int = int(1) / int(0)\n",
            "--check",
        )
        self.assertNotEqual(failing.returncode, 0)
        self.assertIn("constant initializer must be a compile-time constant expression",
                      failing.stderr)

    def test_anerror_subclasses_can_be_raised_and_caught(self):
        result = self.invoke(
            "custom-errors.su",
            "class InputError(AnError)\n"
            "    code: int\n"
            "    def init(message: string, code: int)\n"
            "        self.code = code\n"
            "    end\n"
            "    def get_code(): int\n"
            "        return self.code\n"
            "    end\n"
            "end\n"
            'let error: InputError = InputError("invalid input", int(9))\n'
            "print(error.get_code())\n"
            "try:\n"
            "    raise error\n"
            "catch message:\n"
            "    print(message)\n"
            "end\n"
            "try:\n"
            '    raise AnError("base error")\n'
            "catch message:\n"
            "    print(message)\n"
            "end\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "custom-errors")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "9\ninvalid input\nbase error\n")

    def test_raise_rejects_non_errors_and_invalid_inheritance_bases(self):
        not_error = self.invoke(
            "raise-non-error.su",
            "class Point\n"
            "    x: int\n"
            "end\n"
            "raise Point(int(1))\n",
            "--check",
        )
        self.assertNotEqual(not_error.returncode, 0)
        self.assertIn("raise requires an AnError or a subclass", not_error.stderr)

        unsupported_base = self.invoke(
            "unsupported-base.su",
            "class Child(Missing)\n"
            "end\n",
            "--check",
        )
        self.assertNotEqual(unsupported_base.returncode, 0)
        self.assertIn("base 'Missing' must be a declared non-error class",
                      unsupported_base.stderr)

    def test_except_handlers_are_ordered_and_anerror_is_a_wildcard(self):
        result = self.invoke(
            "except-wildcard.su",
            "class ValidationError(AnError)\n"
            "end\n"
            "try:\n"
            '    raise ValidationError("invalid input")\n'
            "except ValidationError as message:\n"
            '    print("specific: " + message)\n'
            "except AnError as message:\n"
            '    print("wildcard: " + message)\n'
            "end\n"
            "try:\n"
            "    let value: int = int(1) / int(0)\n"
            "except AnError as message:\n"
            '    print("wildcard: " + message)\n'
            "end\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "except-wildcard")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            "specific: invalid input\nwildcard: int division by zero\n",
        )

    def test_except_anerror_must_follow_specific_handlers(self):
        result = self.invoke(
            "except-after-wildcard.su",
            "class ValidationError(AnError)\n"
            "end\n"
            "try:\n"
            '    raise ValidationError("invalid")\n'
            "except AnError:\n"
            '    print("caught")\n'
            "except ValidationError:\n"
            '    print("unreachable")\n'
            "end\n",
            "--check",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("no handler may follow the 'except AnError' wildcard",
                      result.stderr)

    def test_except_anerror_does_not_catch_arbitrary_cpp_exceptions(self):
        result = self.invoke(
            "except-native-exception.su",
            "try:\n"
            "    inline cpp:\n"
            '        throw std::runtime_error("native failure");\n'
            "    endcpp\n"
            "except AnError:\n"
            '    print("caught")\n'
            "end\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "except-native-exception")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertNotEqual(executable.returncode, 0)
        self.assertEqual(executable.stdout, "")
        self.assertIn("native failure", executable.stderr)

    def test_except_cppcatcher_catches_inline_cpp_exceptions_only(self):
        result = self.invoke(
            "except-cpp-catcher.su",
            "try:\n"
            "    inline cpp:\n"
            '        throw std::runtime_error("native failure");\n'
            "    endcpp\n"
            "except cppCatcher as error:\n"
            '    print("caught: " + error)\n'
            "except AnError:\n"
            '    print("Sun error")\n'
            "end\n"
            "try:\n"
            "    inline cpp:\n"
            "        throw 42;\n"
            "    endcpp\n"
            "except cppCatcher as error:\n"
            '    print("caught: " + error)\n'
            "end\n"
            "try:\n"
            "    let value: int = int(1) / int(0)\n"
            "except AnError as error:\n"
            '    print("Sun: " + error)\n'
            "end\n"
            "try:\n"
            "    inline cpp:\n"
            '        throw SunRuntimeError("Sun error from inline");\n'
            "    endcpp\n"
            "except cppCatcher:\n"
            '    print("native error")\n'
            "except AnError as error:\n"
            '    print("Sun: " + error)\n'
            "end\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "except-cpp-catcher")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            "caught: native failure\n"
            "caught: unknown C++ exception in inline cpp\n"
            "Sun: int division by zero\n"
            "Sun: Sun error from inline\n",
        )

    def test_finally_runs_after_normal_handled_and_return_paths(self):
        result = self.invoke(
            "finally.su",
            "def return_value(): int\n"
            "    try:\n"
            "        return int(7)\n"
            "    finally:\n"
            '        print("return cleanup")\n'
            "    end\n"
            "end\n"
            "print(return_value())\n"
            "try:\n"
            '    raise AnError("handled")\n'
            "except AnError:\n"
            '    print("caught")\n'
            "finally:\n"
            '    print("exception cleanup")\n'
            "end\n"
            "try:\n"
            '    print("body")\n'
            "finally:\n"
            '    print("normal cleanup")\n'
            "end\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "finally")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            "return cleanup\n7\ncaught\nexception cleanup\nbody\nnormal cleanup\n",
        )

    def test_finally_runs_before_unhandled_error_reaches_main(self):
        result = self.invoke(
            "finally-unhandled.su",
            "try:\n"
            '    raise AnError("unhandled")\n'
            "finally:\n"
            '    print("cleanup")\n'
            "end\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "finally-unhandled")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 1)
        self.assertEqual(executable.stdout, "cleanup\n")
        self.assertIn("unhandled", executable.stderr)

    def test_finally_rejects_control_flow_that_would_escape_its_cleanup(self):
        result = self.invoke(
            "finally-return.su",
            "def value(): int\n"
            "    try:\n"
            "        return int(1)\n"
            "    finally:\n"
            "        return int(2)\n"
            "    end\n"
            "end\n",
            "--check",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("'return' is not allowed inside a 'finally' block",
                      result.stderr)

    def test_assert_checks_bool_and_throws_catchable_runtime_error(self):
        result = self.invoke(
            "assertion.su",
            "assert(1 < 2)\n"
            "try:\n"
            "    assert(false)\n"
            "catch error:\n"
            "    print(error)\n"
            "end\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "assertion")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "assertion failed\n")

        invalid = self.invoke("assertion-type.su", "assert(1)\n", "--check")
        self.assertNotEqual(invalid.returncode, 0)
        self.assertIn("assert condition must be bool", invalid.stderr)

    def test_constant_folding_preserves_runtime_integer_errors(self):
        cases = [
            ("int(9223372036854774784.0) + int(1024)", "int arithmetic overflow"),
            ("int(-9223372036854775808.0) - int(1)", "int arithmetic overflow"),
            ("int(3037000500) * int(3037000500)", "int arithmetic overflow"),
            ("-int(-9223372036854775808.0)", "int arithmetic overflow"),
            ("int(-9223372036854775808.0) / int(-1)", "int arithmetic overflow"),
            ("int(8) / int(0)", "int division by zero"),
            ("int(8) % int(0)", "int modulo by zero"),
            ("int(9223372036854775808.0)", "int() requires an in-range whole number"),
            ("int(1.5)", "int() requires an in-range whole number"),
        ]
        source = "".join(
            "try:\n"
            f"    print({expression})\n"
            "catch error:\n"
            "    print(error)\n"
            "end\n"
            for expression, _ in cases
        )
        result = self.invoke("constant-integer-errors.su", source, "--compile")
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "constant-integer-errors")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            "".join(f"{message}\n" for _, message in cases),
        )

    def test_int_conversion_accepts_signed_boundaries_in_constant_and_runtime_paths(self):
        result = self.invoke(
            "integer-conversion-boundaries.su",
            "def convert(value: number): int\n"
            "    return int(value)\n"
            "end\n"
            "print(int(-9223372036854775808.0))\n"
            "print(int(9223372036854774784.0))\n"
            "print(convert(-9223372036854775808.0))\n"
            "print(convert(9223372036854774784.0))\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "integer-conversion-boundaries")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            "-9223372036854775808\n"
            "9223372036854774784\n"
            "-9223372036854775808\n"
            "9223372036854774784\n",
        )

    def test_numeric_precedence_fractional_division_and_nested_conditions(self):
        result = self.invoke(
            "operators.su",
            "def check()\n"
            "    let value: number = -9 / 2 + 3 % 2 - 0\n"
            "    if value <= -3:\n"
            "        if value >= -3.5:\n"
            "            if value < 0:\n"
            "                if value > -4:\n"
            "                    if value != 0:\n"
            '                        print(f"{value}")\n'
            "                    end\n"
            "                end\n"
            "            end\n"
            "        end\n"
            "    end\n"
            "    if 1 / 2 == 0.5:\n"
            '        print("fractional")\n'
            "    end\n"
            "end\n"
            "check()\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "operators")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "-3.5\nfractional\n")

    def test_boolean_parameters_conditions_and_interpolation(self):
        result = self.invoke(
            "booleans.su",
            "def show(enabled: bool)\n"
            "    if enabled:\n"
            '        print(f"enabled={enabled}")\n'
            "    else:\n"
            '        print("enabled=%v", %v=enabled)\n'
            "    end\n"
            "end\n"
            "show(true)\n"
            "show(false)\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "booleans")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "enabled=true\nenabled=false\n")

    def test_inline_cpp_global_and_function_blocks(self):
        result = self.invoke(
            "inline-cpp.su",
            "inline cpp:\n"
            "#include <cstdio>\n"
            "static int sun_global_value = 40;\n"
            "endcpp\n"
            "def show(value: number)\n"
            "    let next: number = value + 2\n"
            "    if next == 42:\n"
            "        inline cpp:\n"
            '            std::cout << value << ":" << next << ":" << sun_global_value << "\\n";\n'
            "        endcpp\n"
            "    end\n"
            "end\n"
            "show(40)\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "inline-cpp")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "40:42:40\n")

    def test_inline_cpp_reports_missing_endcpp(self):
        result = self.invoke("open-inline-cpp.su", 'inline cpp:\nstd::cout << "hi";\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("inline C++ block is missing 'endcpp'", result.stderr)

    def test_logical_operators_short_circuit_loops_assignments_and_returns(self):
        result = self.invoke(
            "control-flow.su",
            "def add(a: number, b: number): number\n"
            "    let result: number = a + b\n"
            "    return result\n"
            "end\n"
            "def label(): string\n"
            '    return "total"\n'
            "end\n"
            "def side_effect(): bool\n"
            '    print("called")\n'
            "    return true\n"
            "end\n"
            "def short_circuit(value: bool): bool\n"
            "    return value and side_effect()\n"
            "end\n"
            "def short_or(value: bool): bool\n"
            "    return value or side_effect()\n"
            "end\n"
            "def calculate(): number\n"
            "    let count: number = 0\n"
            "    while count < 3:\n"
            "        count = count + 1\n"
            "    end\n"
            "    let total: number = 0\n"
            "    for i in range(1, 4):\n"
            "        total = total + i\n"
            "    end\n"
            "    for j in range(3, 0, -1):\n"
            "        total = total + j\n"
            "    end\n"
            "    if not false and (count == 3 or total == 0):\n"
            "        return add(total, count)\n"
            "    else:\n"
            "        return -1\n"
            "    end\n"
            "end\n"
            'print("%v=%v", %v=label(), %v=calculate())\n'
            "print(short_circuit(false))\n"
            "print(short_circuit(true))\n"
            "print(short_or(true))\n"
            "print(short_or(false))\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "control-flow")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            "total=15\nfalse\ncalled\ntrue\ntrue\ncalled\ntrue\n",
        )

    def test_non_void_function_requires_return_on_all_paths(self):
        result = self.invoke(
            "missing-return.su",
            "def incomplete(enabled: bool): number\n"
            "    if enabled:\n"
            "        return 1\n"
            "    end\n"
            "end\n",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must return a value on every path", result.stderr)

    def test_rejects_assignment_type_mismatch_and_void_value_use(self):
        assignment = self.invoke(
            "bad-assignment.su",
            'def invalid()\n'
            '    let value: number = 1\n'
            '    value = "text"\n'
            "end\n",
        )
        self.assertNotEqual(assignment.returncode, 0)
        self.assertIn("cannot assign std::string to double", assignment.stderr)

        void_value = self.invoke(
            "void-value.su",
            "def act()\n"
            "end\n"
            "def invalid(): number\n"
            "    return act()\n"
            "end\n",
        )
        self.assertNotEqual(void_value.returncode, 0)
        self.assertIn("does not return a value", void_value.stderr)

    def test_range_rejects_zero_step_at_runtime(self):
        result = self.invoke(
            "zero-range.su",
            "def start()\n"
            "    for i in range(0, 3, 0):\n"
            '        print("unreachable")\n'
            "    end\n"
            "end\n"
            "start()\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "zero-range")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 1)
        self.assertIn("Sun runtime error: range step cannot be zero", executable.stderr)

    def test_break_continue_and_istype(self):
        result = self.invoke(
            "break-continue-istype.su",
            "def total(limit: number): number\n"
            "    let result: number = 0\n"
            "    for i in range(0, limit):\n"
            "        if i == 1:\n"
            "            continue\n"
            "        end\n"
            "        if i == 4:\n"
            "            break\n"
            "        end\n"
            "        result = result + i\n"
            "    end\n"
            "    while true:\n"
            "        result = result + 10\n"
            "        break\n"
            "    end\n"
            "    if istype(result, number) and istype(limit, number) and not istype(result, string):\n"
            "        return result\n"
            "    else:\n"
            "        return 0\n"
            "    end\n"
            "end\n"
            'print("total=%v", %v=total(10))\n',
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "break-continue-istype")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "total=15\n")

    def test_function_parameters_accept_trailing_default_values(self):
        result = self.invoke(
            "default-parameters.su",
            "def describe(name: string = \"world\", times: number = 2): string\n"
            '    return f"Hello {name} {times}"\n'
            "end\n"
            'print(describe())\n'
            'print(describe("Sun"))\n'
            'print(describe("Earth", 3))\n',
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "default-parameters")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            "Hello world 2\nHello Sun 2\nHello Earth 3\n",
        )

    def test_default_parameters_work_in_statement_calls(self):
        result = self.invoke(
            "default-call.su",
            'def greet(name: string = "world")\n'
            '    print(f"Hello {name}")\n'
            "end\n"
            "greet()\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "default-call")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "Hello world\n")

    def test_default_values_must_be_typed_constant_expressions(self):
        wrong_type = self.invoke(
            "default-wrong-type.su",
            'def invalid(value: number = "text")\n'
            "end\n",
        )
        self.assertNotEqual(wrong_type.returncode, 0)
        self.assertIn("default for parameter 'value' must have type double",
                      wrong_type.stderr)

        non_constant = self.invoke(
            "default-variable.su",
            "def invalid(value: number = other)\n"
            "end\n",
        )
        self.assertNotEqual(non_constant.returncode, 0)
        self.assertIn("unknown value 'other'", non_constant.stderr)

    def test_default_parameters_must_be_trailing_and_break_must_be_in_loop(self):
        invalid_defaults = self.invoke(
            "default-order.su",
            "def invalid(first: number = 1, second: number)\n"
            "end\n",
        )
        self.assertNotEqual(invalid_defaults.returncode, 0)
        self.assertIn("parameters without defaults cannot follow", invalid_defaults.stderr)

        invalid_break = self.invoke(
            "break-outside-loop.su",
            "def invalid()\n"
            "    break\n"
            "end\n",
        )
        self.assertNotEqual(invalid_break.returncode, 0)
        self.assertIn("'break' is only valid inside a loop", invalid_break.stderr)

    def test_rejects_math_calls_without_import(self):
        result = self.invoke(
            "missing-math-import.su",
            "def root(x: number)\n"
            "    let value: number = sqrt(x)\n"
            "end\n",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("math functions require 'import math'", result.stderr)

    def test_numeric_and_boolean_collection_builtins(self):
        result = self.invoke(
            "builtins.su",
            "let integers: list[int] = [int(-4), int(7), int(2)]\n"
            "let numbers: list[number] = [1.5, 2.5]\n"
            "let flags: list[bool] = [true, false]\n"
            "let no_flags: list[bool] = []\n"
            "let no_integers: list[int] = []\n"
            "print(sum(integers))\n"
            "print(sum(numbers))\n"
            "print(sum(no_integers))\n"
            "print(sum([int(1), int(2)]))\n"
            "print(all(flags))\n"
            "print(all(no_flags))\n"
            "print(all([true, true]))\n"
            "print(any(flags))\n"
            "print(any(no_flags))\n"
            "print(any([false, true]))\n"
            "print(min(int(-3), 2))\n"
            "print(max(int(4), 4.5))\n"
            "print(round(2.5))\n"
            "print(round(-2.5))\n"
            "print(min(0 / 0, 1))\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "builtins")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            "5\n4\n0\n3\nfalse\ntrue\ntrue\ntrue\nfalse\ntrue\n-3\n4.5\n3\n-3\nnan\n",
        )

    def test_float_builtins_and_decimal_rounding(self):
        result = self.invoke(
            "float-operations.su",
            "print(abs(-2.75))\n"
            "print(floor(2.75))\n"
            "print(ceil(-2.75))\n"
            "print(trunc(-2.75))\n"
            "print(round(3.14159, int(2)))\n"
            "print(round(149, int(-1)))\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "float-operations")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "2.75\n2\n-2\n-2\n3.14\n150\n")

    def test_round_precision_requires_an_integer(self):
        result = self.invoke(
            "round-invalid-precision.su",
            "print(round(3.14159, 2.0))\n",
            "--check",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("round() precision must have type int", result.stderr)

    def test_builtins_reject_incompatible_collection_types(self):
        result = self.invoke(
            "builtin-type-error.su",
            'let words: list[string] = ["hello"]\nprint(sum(words))\n',
            "--check",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("sum() requires a list[int] or list[number]", result.stderr)

    def test_memory_regions_are_bounded_shared_and_typed(self):
        result = self.invoke(
            "memory.su",
            "import memory\n"
            "def store(buffer: region)\n"
            "    memory.write_u8(buffer, 1, 66)\n"
            "end\n"
            "let buffer: region = memory.alloc(4)\n"
            "let alias: region = buffer\n"
            "memory.write_u8(buffer, 0, 65)\n"
            "store(alias)\n"
            'print("size=%v", %v=memory.size(buffer))\n'
            "print(memory.read_u8(alias, 0))\n"
            "print(memory.read_u8(buffer, 1))\n"
            "print(istype(buffer, region))\n"
            "print(istype(buffer, number))\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "memory")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "size=4\n65\n66\ntrue\nfalse\n")

    def test_memory_fill_and_copy_support_overlapping_regions(self):
        result = self.invoke(
            "memory-copy-fill.su",
            "import memory\n"
            "let source: region = memory.alloc(4)\n"
            "memory.fill(source, 46)\n"
            "memory.write_u8(source, 0, 65)\n"
            "memory.write_u8(source, 1, 66)\n"
            "memory.write_u8(source, 2, 67)\n"
            "memory.copy(source, 1, source, 0, 3)\n"
            "let destination: region = memory.alloc(5)\n"
            "memory.fill(destination, 46)\n"
            "memory.copy(destination, 1, source, 1, 3)\n"
            "memory.copy(destination, 5, source, 4, 0)\n"
            "for index in range(0, 5):\n"
            "    print(memory.read_u8(destination, index))\n"
            "end\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "memory-copy-fill")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "46\n65\n66\n67\n46\n")

    def test_memory_multibyte_integers_use_explicit_endianness(self):
        result = self.invoke(
            "memory-multibyte.su",
            "import memory\n"
            "let buffer: region = memory.alloc(12)\n"
            'memory.write_u16(buffer, 0, int(4660), "little")\n'
            'memory.write_u16(buffer, 2, int(4660), "big")\n'
            'memory.write_u32(buffer, 4, int(305419896), "little")\n'
            'memory.write_u32(buffer, 8, int(305419896), "big")\n'
            'let little16: int = memory.read_u16(buffer, 0, "little")\n'
            'let big16: int = memory.read_u16(buffer, 2, "big")\n'
            'let swapped16: int = memory.read_u16(buffer, 0, "big")\n'
            'let little32: int = memory.read_u32(buffer, 4, "little")\n'
            'let big32: int = memory.read_u32(buffer, 8, "big")\n'
            'let swapped32: int = memory.read_u32(buffer, 4, "big")\n'
            'print(little16)\n'
            'print(big16)\n'
            'print(swapped16)\n'
            'print(little32)\n'
            'print(big32)\n'
            'print(swapped32)\n',
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "memory-multibyte")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            "4660\n4660\n13330\n305419896\n305419896\n2018915346\n",
        )

    def test_memory_opaque_pointers_allocate_release_and_interoperate(self):
        result = self.invoke(
            "memory-pointer.su",
            "import memory\n"
            "def allocate(size: int): ptr\n"
            "    return memory.alloc_ptr(size)\n"
            "end\n"
            "let empty: ptr = memory.alloc_ptr(0)\n"
            "print(memory.ptr_is_null(empty))\n"
            "print(memory.ptr_size(empty))\n"
            "memory.free_ptr(empty)\n"
            "let pointer: ptr = allocate(int(4))\n"
            "let alias: ptr = pointer\n"
            "print(memory.ptr_is_null(pointer))\n"
            "print(memory.ptr_size(pointer))\n"
            "print(istype(pointer, ptr))\n"
            "def inspect(pointer: ptr)\n"
            "    inline cpp:\n"
            "        auto* bytes = static_cast<unsigned char*>(pointer.get());\n"
            "        bytes[0] = 65;\n"
            "        std::cout << static_cast<unsigned int>(bytes[0]) << '\\n';\n"
            "    endcpp\n"
            "end\n"
            "inspect(pointer)\n"
            "memory.free_ptr(alias)\n"
            "print(memory.ptr_is_null(pointer))\n"
            "print(memory.ptr_size(alias))\n"
            "memory.free_ptr(pointer)\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "memory-pointer")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "true\n0\nfalse\n4\ntrue\n65\ntrue\n0\n")

    def test_io_reads_writes_appends_and_checks_file_existence(self):
        target = WORK / "io-output.txt"
        result = self.invoke(
            "io.su",
            "import io\n"
            f'let path: string = "{target}"\n'
            'io.write_text(path, "Hello")\n'
            'io.append_text(path, " Sun")\n'
            "let contents: string = io.read_text(path)\n"
            "print(io.exists(path))\n"
            'print(io.exists(path + ".missing"))\n'
            "print(contents)\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "io")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "true\nfalse\nHello Sun\n")
        self.assertEqual(target.read_text(encoding="utf-8"), "Hello Sun")

    def test_bytes_mutate_convert_iterate_and_round_trip_binary_io(self):
        target = WORK / "bytes-output.bin"
        result = self.invoke(
            "bytes.su",
            "import io\n"
            "import memory\n"
            f'let path: string = "{target}"\n'
            "let data: bytes = bytes.from_list([int(0), int(65), int(255)])\n"
            "let empty: bytes = bytes.from_list([])\n"
            "data[1] = int(66)\n"
            "data.append(10)\n"
            "print(len(data))\n"
            "print(len(empty))\n"
            "print(data.contains(255))\n"
            "let from_string: bytes = bytes.from_string(bytes.to_string(data))\n"
            "let buffer: region = bytes.to_region(data)\n"
            "let from_region: bytes = bytes.from_region(buffer)\n"
            "let as_list: list[int] = bytes.to_list(data)\n"
            "print(from_string == data)\n"
            "print(from_region == data)\n"
            "print(as_list == bytes.to_list(from_region))\n"
            "print(istype(data, bytes))\n"
            "io.write_bytes(path, data)\n"
            "let loaded: bytes = io.read_bytes(path)\n"
            "for value in loaded:\n"
            "    print(value)\n"
            "end\n"
            "loaded.clear()\n"
            "print(len(loaded))\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "bytes")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            "4\n0\ntrue\ntrue\ntrue\ntrue\ntrue\n0\n66\n255\n10\n0\n",
        )
        self.assertEqual(target.read_bytes(), bytes([0, 66, 255, 10]))

    def test_bytes_regions_and_custom_values_have_readable_string_forms(self):
        result = self.invoke(
            "value-formatting.su",
            "import memory\n"
            "struct Point\n"
            "    x: int\n"
            "    y: int\n"
            "end\n"
            "class NamedPoint\n"
            "    name: string\n"
            "    values: list[int]\n"
            "    def init(name: string)\n"
            "        self.name = name\n"
            "        self.values = [int(3), int(4)]\n"
            "    end\n"
            "end\n"
            "class LabeledPoint(NamedPoint)\n"
            "    label: string\n"
            "end\n"
            "let empty: bytes = bytes.from_list([])\n"
            "let data: bytes = bytes.from_list([int(0), int(65), int(255)])\n"
            "let region: region = memory.alloc(0)\n"
            "let point: Point = Point(int(2), int(5))\n"
            'let named: LabeledPoint = LabeledPoint("origin")\n'
            'named.label = "start"\n'
            "print(empty)\n"
            "print(data)\n"
            'print(f"bytes={data}; empty={empty}")\n'
            'print("formatted %v | %v | %v", %v=data, %v=region, %v=point)\n'
            "print(region)\n"
            "print(f\"{region}\")\n"
            "print(point)\n"
            "print(f\"{named}\")\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "value-formatting")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            "[]\n[0, 65, 255]\n"
            "bytes=[0, 65, 255]; empty=[]\n"
            "formatted [0, 65, 255] | region(size=0) | Point{x=2, y=5}\n"
            "region(size=0)\nregion(size=0)\n"
            "Point{x=2, y=5}\n"
            "LabeledPoint{name=origin, values=[3, 4], label=start}\n",
        )

    def test_bytes_reject_invalid_values_and_indexes_at_runtime(self):
        cases = [
            (
                "bytes-invalid-value.su",
                "let data: bytes = bytes.from_list([int(1)])\n"
                "data[0] = 256\n",
                "byte value must be an integer from 0 to 255",
            ),
            (
                "bytes-invalid-index.su",
                "let data: bytes = bytes.from_list([int(1)])\n"
                "print(data[1])\n",
                "bytes index out of bounds",
            ),
        ]
        for source_name, source, expected_error in cases:
            with self.subTest(source=source_name):
                result = self.invoke(source_name, source, "--compile")
                self.assertEqual(result.returncode, 0, result.stderr)
                executable = subprocess.run(
                    [str(WORK / source_name[:-3])],
                    cwd=ROOT,
                    text=True,
                    capture_output=True,
                    check=False,
                )
                self.assertNotEqual(executable.returncode, 0)
                self.assertIn(expected_error, executable.stderr)

    def test_io_lines_directories_file_size_and_remove(self):
        directory = WORK / "io-extra-dir"
        file_path = directory / "lines.txt"
        result = self.invoke(
            "io-extra.su",
            "import io\n"
            f'let directory: string = "{directory}"\n'
            f'let path: string = "{file_path}"\n'
            "print(io.create_directories(directory))\n"
            'io.write_text(path, "first\\nsecond\\n")\n'
            "print(io.is_directory(directory))\n"
            "print(io.file_size(path))\n"
            "print(io.read_lines(path))\n"
            "print(io.remove(path))\n"
            "print(io.exists(path))\n"
            "print(io.remove(directory))\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "io-extra")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            "true\ntrue\n13\n[first, second]\ntrue\nfalse\ntrue\n",
        )

    def test_io_createdir_exist_modes_and_recursive_creation(self):
        directory = WORK / "createdir-root" / "nested"
        file_path = directory / "existing.txt"
        result = self.invoke(
            "io-createdir.su",
            "import io\n"
            f'let directory: string = "{directory}"\n'
            f'let file_path: string = "{file_path}"\n'
            "print(io.createdir(directory))\n"
            'print(io.createdir(directory, exist="ok"))\n'
            "try:\n"
            '    let denied: bool = io.createdir(directory, exist="no")\n'
            "catch error:\n"
            "    print(error)\n"
            "end\n"
            'io.write_text(file_path, "file")\n'
            "try:\n"
            '    let not_directory: bool = io.createdir(file_path, exist="ok")\n'
            "catch error:\n"
            "    print(error)\n"
            "end\n"
            "try:\n"
            '    let invalid: bool = io.createdir(directory, exist="maybe")\n'
            "catch error:\n"
            "    print(error)\n"
            "end\n"
            "print(io.is_directory(directory))\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "io-createdir")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout.splitlines()[0:2], ["true", "false"])
        self.assertIn("io.createdir directory already exists", executable.stdout)
        self.assertIn("io.createdir path exists and is not a directory", executable.stdout)
        self.assertIn("io.createdir exist must be 'ok' or 'no'", executable.stdout)
        self.assertTrue(executable.stdout.endswith("true\n"))

    def test_io_requires_import_and_reports_read_errors(self):
        missing_import = self.invoke(
            "io-missing-import.su",
            'let exists: bool = io.exists("missing.txt")\n',
        )
        self.assertNotEqual(missing_import.returncode, 0)
        self.assertIn("io functions require 'import io'", missing_import.stderr)

        result = self.invoke(
            "io-read-error.su",
            "import io\n"
            'try:\n'
            '    let contents: string = io.read_text("missing-file-for-io-test.txt")\n'
            "catch error:\n"
            "    print(error)\n"
            "end\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "io-read-error")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertIn("io.read_text could not open file", executable.stdout)

    def test_io_open_with_close_binary_and_partial_line_write(self):
        target = WORK / "io-open.txt"
        result = self.invoke(
            "io-open.su",
            "import io\n"
            f'let path: string = "{target}"\n'
            "with open(path, \"w\") as output:\n"
            '    io.write(output, "first\\nsecond\\n")\n'
            "end\n"
            "let handle: int = io.open(path, \"pw\")\n"
            'io.pw(handle, int(2), "updated")\n'
            "io.close(handle)\n"
            "def read_file(path: string): string\n"
            "    with open(path, \"rb\") as input_file:\n"
            "        return io.read(input_file)\n"
            "    end\n"
            "end\n"
            "print(read_file(path))\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "io-open")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "first\nupdated\n\n")
        self.assertEqual(target.read_bytes(), b"first\nupdated\n")

    def test_io_open_append_mode_preserves_and_creates_file(self):
        target = WORK / "io-append.txt"
        result = self.invoke(
            "io-append.su",
            "import io\n"
            f'let path: string = "{target}"\n'
            "with open(path, \"a\") as output:\n"
            '    io.write(output, "first\\n")\n'
            "end\n"
            "let handle: int = open(path, \"a\")\n"
            'write(handle, "second\\n")\n'
            "close(handle)\n"
            "print(io.read_text(path))\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "io-append")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "first\nsecond\n\n")
        self.assertEqual(target.read_bytes(), b"first\nsecond\n")

    def test_json_loads_dumps_nested_access_and_assignment(self):
        result = self.invoke(
            "json.su",
            "import json\n"
            'let data: json = json.loads("{\\"profile\\":{\\"name\\":\\"Ada\\"},\\"items\\":[1,true,null]}")\n'
            'let fixed: json = json.loads("{\\"value\\":3}")\n'
            'print(fixed["value"].as_int())\n'
            'print(data["profile"]["name"].as_string())\n'
            'print(data["items"][0].as_int())\n'
            'print(data["items"][1].as_bool())\n'
            'print(data["profile"].contains("name"))\n'
            'print(data["profile"].contains("missing"))\n'
            'data["profile"]["name"] = "Grace"\n'
            'data["items"][0] = int(2)\n'
            'let appended: int = data["items"].append("tail")\n'
            'data["items"].append(data["profile"])\n'
            "print(appended)\n"
            "print(json.dumps(data))\n"
            'let labels: dict[string, string] = {"name": "Ada"}\n'
            "print(json.dumps(labels))\n"
            "let values: list[int] = [int(1), int(2)]\n"
            "print(json.dumps(values))\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "json")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            '3\nAda\n1\ntrue\ntrue\nfalse\n4\n'
            '{"items":[2,true,null,"tail",{"name":"Grace"}],"profile":{"name":"Grace"}}\n'
            '{"name":"Ada"}\n[1,2]\n',
        )

    def test_json_mutators_reject_wrong_container(self):
        result = self.invoke(
            "json-mutator-types.su",
            "import json\n"
            'let object: json = json.loads("{}")\n'
            'let array: json = json.loads("[]")\n'
            "try:\n"
            "    object.append(1)\n"
            "catch error:\n"
            "    print(error)\n"
            "end\n"
            "try:\n"
            '    print(array.contains("key"))\n'
            "catch error:\n"
            "    print(error)\n"
            "end\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "json-mutator-types")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            "JSON value is not an array\nJSON value is not an object\n",
        )

    def test_json_integer_boundaries_preserve_large_integer_values(self):
        result = self.invoke(
            "json-integer-boundaries.su",
            "import json\n"
            'print(json.loads("-9223372036854775808").as_int())\n'
            'print(json.loads("9223372036854774784").as_int())\n'
            'print(json.loads("[42]")[0].as_int())\n'
            'print(json.loads("1.2e1").as_int())\n'
            'print(json.loads("1.0") == json.loads("1.00"))\n'
            'print(json.loads("1.0") != json.loads("2.0"))\n'
            'let below: json = json.loads("-9223372036854775809")\n'
            'let above: json = json.loads("9223372036854775808")\n'
            'let fractional: json = json.loads("-9223372036854775808.1")\n'
            "print(json.dumps(below))\n"
            "print(json.dumps(above))\n"
            "try:\n"
            "    print(below.as_int())\n"
            "catch error:\n"
            "    print(error)\n"
            "end\n"
            "try:\n"
            "    print(above.as_int())\n"
            "catch error:\n"
            "    print(error)\n"
            "end\n"
            "try:\n"
            "    print(fractional.as_int())\n"
            "catch error:\n"
            "    print(error)\n"
            "end\n"
            "try:\n"
            '    let out_of_range: json = json.loads("1e400")\n'
            "catch error:\n"
            "    print(error)\n"
            "end\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "json-integer-boundaries")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            "-9223372036854775808\n"
            "9223372036854774784\n"
            "42\n"
            "12\n"
            "true\n"
            "true\n"
            "-9223372036854775809\n"
            "9223372036854775808\n"
            "json value is not an in-range integer\n"
            "json value is not an in-range integer\n"
            "json value is not an in-range integer\n"
            "json.loads number is out of range at byte 5\n",
        )

    def test_json_requires_import_and_reports_invalid_json(self):
        missing_import = self.invoke(
            "json-missing-import.su",
            'let value: json = json.loads("null")\n',
        )
        self.assertNotEqual(missing_import.returncode, 0)
        self.assertIn("the json type requires 'import json'", missing_import.stderr)

        result = self.invoke(
            "json-invalid.su",
            "import json\n"
            'try:\n'
            '    let value: json = json.loads("{invalid}")\n'
            "catch error:\n"
            "    print(error)\n"
            "end\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "json-invalid")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertIn("json.loads", executable.stdout)

    def test_local_modules_can_import_stdlib_json(self):
        helper = WORK / "json_reader.su"
        helper.write_text(
            "import json\n"
            "export def read_value(): json\n"
            '    return json.loads("{\\"value\\":7}")\n'
            "end\n",
            encoding="utf-8",
        )
        result = self.invoke(
            "json-local.su",
            "import local/json_reader as reader\n"
            "let value: json = reader.read_value()\n"
            'print(value["value"].as_int())\n',
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "json-local")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "7\n")

    def test_memory_requires_import_and_formats_regions_as_text(self):
        missing_import = self.invoke(
            "memory-missing-import.su",
            "let size: number = memory.size(1)\n",
        )
        self.assertNotEqual(missing_import.returncode, 0)
        self.assertIn("memory functions require 'import memory'", missing_import.stderr)
        missing_pointer_import = self.invoke(
            "pointer-missing-import.su",
            "let pointer: ptr = memory.alloc_ptr(4)\n",
        )
        self.assertNotEqual(missing_pointer_import.returncode, 0)
        self.assertIn("the ptr type requires 'import memory'",
                      missing_pointer_import.stderr)

        print_region = self.invoke(
            "memory-print-region.su",
            "import memory\n"
            "let buffer: region = memory.alloc(1)\n"
            "print(buffer)\n",
            "--compile",
        )
        self.assertEqual(print_region.returncode, 0, print_region.stderr)
        executable = subprocess.run(
            [str(WORK / "memory-print-region")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "region(size=1)\n")

    def test_memory_runtime_checks_allocation_offsets_and_byte_values(self):
        cases = [
            (
                "memory-bounds.su",
                "import memory\n"
                "let buffer: region = memory.alloc(1)\n"
                "print(memory.read_u8(buffer, 1))\n",
                "memory offset out of bounds",
            ),
            (
                "memory-byte-value.su",
                "import memory\n"
                "let buffer: region = memory.alloc(1)\n"
                "memory.write_u8(buffer, 0, 256)\n",
                "memory.write_u8 value must be an integer from 0 to 255",
            ),
            (
                "memory-fill-byte-value.su",
                "import memory\n"
                "let buffer: region = memory.alloc(1)\n"
                "memory.fill(buffer, 256)\n",
                "memory.fill value must be an integer from 0 to 255",
            ),
            (
                "memory-u16-value.su",
                "import memory\n"
                "let buffer: region = memory.alloc(2)\n"
                'memory.write_u16(buffer, 0, 65536, "little")\n',
                "memory.write_u16 value must be an integer from 0 to 65535",
            ),
            (
                "memory-u32-value.su",
                "import memory\n"
                "let buffer: region = memory.alloc(4)\n"
                'memory.write_u32(buffer, 0, 4294967296, "little")\n',
                "memory.write_u32 value must be an integer from 0 to 4294967295",
            ),
            (
                "memory-u32-endian.su",
                "import memory\n"
                "let buffer: region = memory.alloc(4)\n"
                'memory.write_u32(buffer, 0, 1, "middle")\n',
                "memory.write_u32 endian must be 'little' or 'big'",
            ),
            (
                "memory-u32-bounds.su",
                "import memory\n"
                "let buffer: region = memory.alloc(3)\n"
                'print(memory.read_u32(buffer, 0, "big"))\n',
                "memory range out of bounds",
            ),
            (
                "memory-copy-bounds.su",
                "import memory\n"
                "let buffer: region = memory.alloc(1)\n"
                "memory.copy(buffer, 0, buffer, 0, 2)\n",
                "memory range out of bounds",
            ),
            (
                "memory-allocation-limit.su",
                "import memory\n"
                "let buffer: region = memory.alloc(67108865)\n",
                "memory.alloc size must be an integer from 0 to 67108864",
            ),
            (
                "memory-pointer-allocation-limit.su",
                "import memory\n"
                "let pointer: ptr = memory.alloc_ptr(67108865)\n",
                "memory.alloc_ptr size must be an integer from 0 to 67108864",
            ),
        ]
        for source_name, source, expected_error in cases:
            with self.subTest(source=source_name):
                result = self.invoke(source_name, source, "--compile")
                self.assertEqual(result.returncode, 0, result.stderr)
                executable = subprocess.run(
                    [str(WORK / source_name[:-3])],
                    cwd=ROOT,
                    text=True,
                    capture_output=True,
                    check=False,
                )
                self.assertEqual(executable.returncode, 1)
                self.assertIn(expected_error, executable.stderr)

    def test_typed_lists_indexing_append_iteration_and_empty_initialization(self):
        result = self.invoke(
            "lists.su",
            "def total(values: list[int]): int\n"
            "    let result: int = int(0)\n"
            "    for value in values:\n"
            "        result = result + value\n"
            "    end\n"
            "    return result\n"
            "end\n"
            "def identity(values: list[int]): list[int]\n"
            "    return values\n"
            "end\n"
            "let values: list[int] = []\n"
            "values.append(int(4))\n"
            "values.append(int(5))\n"
            "values[0] = values[0] + int(1)\n"
            "let flags: list[bool] = [true, false]\n"
            "flags[1] = true\n"
            'let message: string = "array[index] and list.append("\n'
            'print("items=%v total=%v length=%v", %v=values, %v=total(identity(values)), %v=len(values))\n'
            "print(flags)\n"
            "print(message)\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "lists")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            "items=[5, 5] total=10 length=2\n[true, true]\n"
            "array[index] and list.append(\n",
        )

    def test_lists_reject_mixed_items_and_check_indexes_at_runtime(self):
        mixed = self.invoke("mixed-list.su", 'let values: list[int] = [int(1), "two"]\n')
        self.assertNotEqual(mixed.returncode, 0)
        self.assertIn("list items must all have the same type", mixed.stderr)

        result = self.invoke(
            "list-bounds.su",
            "let values: list[number] = [3]\n"
            "print(values[-1])\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "list-bounds")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 1)
        self.assertIn("list index out of bounds", executable.stderr)

    def test_typed_input_and_runtime_catch(self):
        result = self.invoke(
            "input-catch.su",
            "let name: string = input(\"name? \")\n"
            "let amount: int = input(\"amount? \", int)\n"
            "let enabled: bool = input(\"enabled? \", bool)\n"
            "let initial: char = input(\"initial? \", char)\n"
            "let ratio: number = input(\"ratio? \", number)\n"
            "print(f\"{name} {amount} {enabled} {initial} {ratio}\")\n"
            "try:\n"
            "    let invalid: int = int(2.5)\n"
            "catch e:\n"
            '    print(f"caught: {e}")\n'
            "end\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "input-catch")],
            cwd=ROOT,
            input="Sun\n7\ntrue\nS\n1.5\n",
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            "name? amount? enabled? initial? ratio? Sun 7 true S 1.5\n"
            "caught: int() requires an in-range whole number\n",
        )

    def test_input_accepts_str_as_string_type_alias(self):
        result = self.invoke(
            "input-str-alias.su",
            'let name: str = input("Name: ", str)\n'
            "print(name)\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "input-str-alias")],
            cwd=ROOT,
            input="Sun\n",
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "Name: Sun\n")

    def test_local_module_alias_and_standard_library_precedence(self):
        (WORK / "math.su").write_text(
            "def sqrt(value: number): number\n"
            "    def increment(value: number): number\n"
            "        return value + 1\n"
            "    end\n"
            "    return increment(value)\n"
            "end\n",
            encoding="utf-8",
        )
        result = self.invoke(
            "module-import.su",
            "import math\n"
            "import local/math as (local_math)\n"
            "let standard: number = math.sqrt(81)\n"
            "let local: number = local_math.sqrt(4)\n"
            "print(f\"{standard} {local}\")\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "module-import")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "9 5\n")

    def test_local_modules_can_mark_public_exports_and_use_private_helpers(self):
        (WORK / "public_tools.su").write_text(
            "export def public_value(): int\n"
            "    return private_helper()\n"
            "end\n"
            "def private_helper(): int\n"
            "    return int(42)\n"
            "end\n"
            "def hidden_value(): int\n"
            "    return int(99)\n"
            "end\n",
            encoding="utf-8",
        )
        result = self.invoke(
            "explicit-module-exports.su",
            "import local/public_tools as tools\n"
            "print(tools.public_value())\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "explicit-module-exports")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "42\n")

    def test_local_modules_reject_importing_unexported_functions(self):
        (WORK / "private_tools.su").write_text(
            "export def visible(): int\n"
            "    return int(1)\n"
            "end\n"
            "def hidden(): int\n"
            "    return int(2)\n"
            "end\n",
            encoding="utf-8",
        )
        result = self.invoke(
            "private-module-export.su",
            "import local/private_tools as tools\n"
            "print(tools.hidden())\n",
            "--check",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("module alias 'tools' does not export function 'hidden'",
                      result.stderr)

    def test_local_modules_can_import_stdlib_io(self):
        (WORK / "file_tools.su").write_text(
            "import io\n"
            "def exists(path: string): bool\n"
            "    return io.exists(path)\n"
            "end\n",
            encoding="utf-8",
        )
        result = self.invoke(
            "io-module-import.su",
            "import local/file_tools as files\n"
            'print(files.exists("a-file-that-does-not-exist"))\n',
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "io-module-import")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "false\n")

    def test_sys_standard_module_exposes_platform_args_and_exit(self):
        result = self.invoke(
            "sys-module.su",
            "import sys\n"
            "print(sys.args())\n"
            "print(sys.platform())\n"
            "sys.exit(int(7))\n"
            'print("unreachable")\n',
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "sys-module"), "first", "two words"],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 7, executable.stderr)
        self.assertEqual(executable.stdout.splitlines()[0], "[first, two words]")
        self.assertIn(
            executable.stdout.splitlines()[1],
            {"linux", "windows", "macos", "unix", "unknown"},
        )
        self.assertNotIn("unreachable", executable.stdout)

    def test_sys_exposes_cwd_and_environment_helpers(self):
        result = self.invoke(
            "sys-environment.su",
            "import sys\n"
            "print(sys.cwd())\n"
            'print(sys.env_exists("SUN_TEST_VALUE"))\n'
            'print(sys.env("SUN_TEST_VALUE", "fallback"))\n'
            'print(sys.env("SUN_MISSING_VALUE", "fallback"))\n',
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        environment = os.environ.copy()
        environment["SUN_TEST_VALUE"] = "present"
        executable = subprocess.run(
            [str(WORK / "sys-environment")],
            cwd=ROOT,
            env=environment,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            f"{ROOT}\ntrue\npresent\nfallback\n",
        )

    def test_local_modules_can_import_sys_stdlib(self):
        (WORK / "cli_tools.su").write_text(
            "import sys\n"
            "def arguments(): list[string]\n"
            "    return sys.args()\n"
            "end\n",
            encoding="utf-8",
        )
        result = self.invoke(
            "sys-local-module.su",
            "import local/cli_tools as cli\n"
            "print(cli.arguments())\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "sys-local-module"), "payload"],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "[payload]\n")

    def test_structs_enums_and_simple_classes(self):
        result = self.invoke(
            "custom-types.su",
            "enum Color\n"
            "    Red\n"
            "    Blue\n"
            "end\n"
            "struct Point\n"
            "    x: int\n"
            "    y: int\n"
            "end\n"
            "class Counter\n"
            "    value: int\n"
            "    def init(start: int)\n"
            "        self.value = start\n"
            "    end\n"
            "    def increment()\n"
            "        self.value = self.value + int(1)\n"
            "    end\n"
            "    def get(): int\n"
            "        return self.value\n"
            "    end\n"
            "end\n"
            "let point: Point = Point(int(2), int(3))\n"
            "let color: Color = Color.Blue\n"
            "let counter: Counter = Counter(int(4))\n"
            "counter.increment()\n"
            "print(point.x)\n"
            "print(color)\n"
            "print(counter.get())\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "custom-types")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "2\nBlue\n5\n")

    def test_classes_support_single_inheritance_overrides_and_base_init(self):
        result = self.invoke(
            "class-inheritance.su",
            "class Animal\n"
            "    name: string\n"
            "    def init(name: string)\n"
            "        self.name = name\n"
            "    end\n"
            "    def label(): string\n"
            '        return "Animal: " + self.name\n'
            "    end\n"
            "    def sound(): string\n"
            '        return "?"\n'
            "    end\n"
            "end\n"
            "class Dog(Animal)\n"
            "    age: int\n"
            "    def init(name: string, age: int)\n"
            "        super.init(name)\n"
            "        self.age = age\n"
            "    end\n"
            "    def sound(): string\n"
            '        return "woof"\n'
            "    end\n"
            "    def age_years(): int\n"
            "        return self.age\n"
            "    end\n"
            "end\n"
            "class Pet(Animal)\n"
            "end\n"
            "class Puppy(Pet)\n"
            "    def init(name: string)\n"
            "        super.init(name)\n"
            "    end\n"
            "end\n"
            'let dog: Dog = Dog(name="Rex", age=int(4))\n'
            'let pet: Pet = Pet("Milo")\n'
            'let puppy: Puppy = Puppy("Pip")\n'
            "print(dog.label())\n"
            "print(dog.sound())\n"
            "print(dog.age_years())\n"
            "print(pet.label())\n"
            "print(puppy.label())\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "class-inheritance")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            "Animal: Rex\nwoof\n4\nAnimal: Milo\nAnimal: Pip\n",
        )

    def test_derived_classes_without_init_can_construct_inherited_fields(self):
        result = self.invoke(
            "class-inherited-fields.su",
            "class Base\n"
            "    id: int\n"
            "end\n"
            "class Child(Base)\n"
            "    active: bool\n"
            "end\n"
            "let item: Child = Child(active=true, id=int(12))\n"
            "print(item.id)\n"
            "print(item.active)\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "class-inherited-fields")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "12\ntrue\n")

    def test_class_inheritance_rejects_invalid_order_and_overrides(self):
        cases = [
            (
                "base-declared-later",
                "class Child(Base)\nend\nclass Base\nend\n",
                "base class 'Base' must be declared before derived classes",
            ),
            (
                "multiple-bases",
                "class A\nend\nclass B\nend\nclass C(A, B)\nend\n",
                "classes support exactly one base class",
            ),
            (
                "struct-base",
                "struct Base\n    id: int\nend\nclass Child(Base)\nend\n",
                "base 'Base' must be a declared non-error class",
            ),
            (
                "mismatched-override",
                "class Base\n"
                "    def value(x: int): int\n"
                "        return x\n"
                "    end\n"
                "end\n"
                "class Child(Base)\n"
                "    def value(x: number): int\n"
                "        return int(x)\n"
                "    end\n"
                "end\n",
                "override of 'value' must match the inherited signature",
            ),
            (
                "base-init-not-first",
                "class Base\n"
                "    def init(value: int)\n"
                "    end\n"
                "end\n"
                "class Child(Base)\n"
                "    def init(value: int)\n"
                "        self.value = value\n"
                "        super.init(value)\n"
                "    end\n"
                "end\n",
                "derived init must begin with super.init(...)",
            ),
        ]
        for source_name, source, expected_error in cases:
            with self.subTest(source=source_name):
                result = self.invoke(source_name + ".su", source, "--check")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(expected_error, result.stderr)

    def test_enum_rejects_unknown_variant(self):
        result = self.invoke(
            "invalid-enum.su",
            "enum Color\n    Red\nend\n"
            "let color: Color = Color.Green\n",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unknown value 'Green' in enum 'Color'", result.stderr)

    def test_payload_enums_and_match(self):
        result = self.invoke(
            "enum-match.su",
            "enum Result\n"
            "    Ok(value: number)\n"
            "    Error(message: string)\n"
            "    Pending\n"
            "end\n"
            "let result: Result = Result.Ok(42)\n"
            "print(result)\n"
            "match result:\n"
            "    case Result.Ok(value):\n"
            "        value = value + 1\n"
            "        print(value)\n"
            "    case Result.Error(message):\n"
            "        print(message)\n"
            "    else:\n"
            '        print("pending")\n'
            "end\n"
            "let status: number = 2\n"
            "match status:\n"
            "    case 1:\n"
            '        print("one")\n'
            "    case 2:\n"
            '        print("two")\n'
            "    else:\n"
            '        print("other")\n'
            "end\n"
            "let count: int = int(1)\n"
            "match count:\n"
            "    case int(1):\n"
            '        print("integer")\n'
            "    else:\n"
            '        print("other integer")\n'
            "end\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "enum-match")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "Ok\n43\ntwo\ninteger\n")

    def test_match_rejects_duplicate_and_uncovered_cases(self):
        incomplete_enum = self.invoke(
            "incomplete-enum-match.su",
            "enum State\n"
            "    Ready\n"
            "    Busy\n"
            "end\n"
            "let state: State = State.Ready\n"
            "match state:\n"
            "    case State.Ready:\n"
            '        print("ready")\n'
            "end\n",
            "--check",
        )
        self.assertNotEqual(incomplete_enum.returncode, 0)
        self.assertIn("match is missing enum variant 'Busy'", incomplete_enum.stderr)

        duplicate_case = self.invoke(
            "duplicate-match-case.su",
            "let active: bool = true\n"
            "match active:\n"
            "    case true:\n"
            '        print("yes")\n'
            "    case true:\n"
            '        print("also yes")\n'
            "    else:\n"
            '        print("no")\n'
            "end\n",
            "--check",
        )
        self.assertNotEqual(duplicate_case.returncode, 0)
        self.assertIn("duplicate match case pattern", duplicate_case.stderr)

        incomplete_bool = self.invoke(
            "incomplete-bool-match.su",
            "let active: bool = true\n"
            "match active:\n"
            "    case true:\n"
            '        print("yes")\n'
            "end\n",
            "--check",
        )
        self.assertNotEqual(incomplete_bool.returncode, 0)
        self.assertIn("match on bool must cover 'true' and 'false'",
                      incomplete_bool.stderr)

    def test_match_accepts_exhaustive_enum_and_bool_without_else(self):
        result = self.invoke(
            "exhaustive-match.su",
            "enum State\n"
            "    Ready\n"
            "    Busy\n"
            "end\n"
            "def state_name(state: State): string\n"
            "    match state:\n"
            "        case State.Ready:\n"
            '            return "ready"\n'
            "        case State.Busy:\n"
            '            return "busy"\n'
            "    end\n"
            "end\n"
            "def bool_name(value: bool): string\n"
            "    match value:\n"
            "        case true:\n"
            '            return "true"\n'
            "        case false:\n"
            '            return "false"\n'
            "    end\n"
            "end\n"
            "print(state_name(State.Busy))\n"
            "print(bool_name(false))\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "exhaustive-match")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "busy\nfalse\n")

    def test_nested_functions_capture_variables_by_reference(self):
        result = self.invoke(
            "nested-functions.su",
            "def run(): number\n"
            "    let total: number = 0\n"
            "    def add(value: number)\n"
            "        total = total + value\n"
            "    end\n"
            "    def sum(value: int): int\n"
            "        if value <= int(0):\n"
            "            return int(0)\n"
            "        end\n"
            "        return value + sum(value - int(1))\n"
            "    end\n"
            "    add(3)\n"
            "    add(4)\n"
            "    return total + number(sum(int(4)))\n"
            "end\n"
            "print(run())\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "nested-functions")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "17\n")

    def test_nested_function_is_not_visible_outside_its_scope(self):
        result = self.invoke(
            "nested-function-scope.su",
            "def outer()\n"
            "    def hidden()\n"
            "        print(\"hidden\")\n"
            "    end\n"
            "end\n"
            "outer()\n"
            "hidden()\n",
            "--check",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unknown function 'hidden'", result.stderr)

    def test_try_catches_input_errors_but_not_compile_errors(self):
        result = self.invoke(
            "input-error.su",
            "try:\n"
            '    let value: int = input("number? ", int)\n'
            "catch error:\n"
            '    print(f"invalid: {error}")\n'
            "end\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "input-error")],
            cwd=ROOT,
            input="not a number\n",
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "number? invalid: input expected an int\n")

        compile_error = self.invoke(
            "try-compile-error.su",
            "try:\n"
            "    print(missing_value)\n"
            "catch error:\n"
            '    print("not reached")\n'
            "end\n",
        )
        self.assertNotEqual(compile_error.returncode, 0)
        self.assertIn("unknown value 'missing_value'", compile_error.stderr)

    def test_int_math_conversions_and_char_type(self):
        result = self.invoke(
            "integer-char.su",
            "let first: int = int(7)\n"
            "let second: int = int(2)\n"
            "let quotient: int = first / second\n"
            "let promoted: number = first + 0.5\n"
            'let initial: char = char("A")\n'
            "print(f\"{quotient} {promoted} {initial}\")\n"
            "print(istype(first, int))\n"
            "print(istype(initial, char))\n"
            "try:\n"
            "    let overflow: int = int(9223372036854774784.0) + int(2048)\n"
            "catch error:\n"
            '    print(error)\n'
            "end\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "integer-char")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            "3 7.5 A\ntrue\ntrue\nint arithmetic overflow\n",
        )

    def test_char_supports_unicode_codepoints(self):
        result = self.invoke(
            "unicode-char.su",
            'let accented: char = char("é")\n'
            'let emoji: char = char("🌞")\n'
            'print(f"{accented}{emoji}")\n',
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "unicode-char")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "é🌞\n")

    def test_rejects_incompatible_static_types(self):
        result = self.invoke(
            "wrong-type.su",
            'def invalid()\n'
            '    let count: number = "three"\n'
            "end\n",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("cannot initialize double with a std::string value", result.stderr)
        self.assertIn("= help: Make the expression's type match", result.stderr)

    def test_reports_unknown_function_with_source_line(self):
        result = self.invoke("bad-call.su", 'print("start")\nmissing()\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("error: unknown function 'missing'", result.stderr)
        self.assertIn("bad-call.su:2:1", result.stderr)
        self.assertIn("  2 | missing()", result.stderr)
        self.assertIn("    | ^^^^^^^", result.stderr)
        self.assertFalse((WORK / "bad-call.cpp").exists())

    def test_unknown_function_in_expression_has_a_direct_explanation(self):
        result = self.invoke(
            "unknown-expression-call.su",
            "let result: number = missing()\n",
            "--check",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("error: unknown function 'missing'", result.stderr)
        self.assertIn("= help: Check the spelling or declare the function", result.stderr)

    def test_reports_missing_end(self):
        result = self.invoke("open-block.su", "def hello()\nprint(\"hi\")\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("error: function 'hello' is missing 'end'", result.stderr)
        self.assertIn("= help: Add 'end' to close the function", result.stderr)

    def test_rejects_wrong_argument_count(self):
        result = self.invoke("wrong-args.su", 'def greet(name)\nend\ngreet()\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("error: function 'greet' expects 1 argument(s), got 0",
                      result.stderr)
        self.assertIn("= help: Check the function declaration", result.stderr)

    def test_string_nul_escape_and_raw_nul_boundary(self):
        result = self.invoke(
            "nul-string.su",
            'let value: string = "before\\0after"\n'
            "let digit: string = \"A\\01\"\n"
            "print(len(value))\n"
            "print(value)\n"
            "print(len(digit))\n"
            "print(digit)\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "nul-string")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "12\nbefore\x00after\n3\nA\x001\n")

        raw_nul = self.invoke("raw-nul-string.su", 'print("before\x00after")\n')
        self.assertNotEqual(raw_nul.returncode, 0)
        self.assertIn("error: NUL byte is not allowed in string literal", raw_nul.stderr)
        self.assertFalse((WORK / "raw-nul-string.cpp").exists())


if __name__ == "__main__":
    unittest.main()
