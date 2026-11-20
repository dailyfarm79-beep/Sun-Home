#include "compiler.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sun {
namespace {

struct Value {
    Type type;
    std::string operand;
    std::string string_length = "0";
};

bool is_string_like(Type type) {
    return type == Type::string || type == Type::character;
}

bool is_list_type(Type type) {
    return type.kind == Type::Kind::list_number ||
           type.kind == Type::Kind::list_integer ||
           type.kind == Type::Kind::list_string ||
           type.kind == Type::Kind::list_boolean ||
           type.kind == Type::Kind::list_character;
}

Type list_item_type(Type type) {
    if (type.kind == Type::Kind::list_number) return Type::number;
    if (type.kind == Type::Kind::list_integer) return Type::integer;
    if (type.kind == Type::Kind::list_string) return Type::string;
    if (type.kind == Type::Kind::list_boolean) return Type::boolean;
    if (type.kind == Type::Kind::list_character) return Type::character;
    throw std::logic_error("not a Sun list type");
}

std::string llvm_list_suffix(Type type) {
    if (type == Type::integer) return "i64";
    if (type == Type::number) return "double";
    if (type == Type::boolean) return "bool";
    if (type == Type::string || type == Type::character) return "string";
    throw std::logic_error("unsupported LLVM list item type '" + cpp_type(type) + "'");
}

std::string llvm_type(Type type) {
    if (is_list_type(type) || type.kind == Type::Kind::empty_list) return "ptr";
    if (type == Type::integer) return "i64";
    if (type == Type::number) return "double";
    if (type == Type::boolean) return "i1";
    if (is_string_like(type)) return "ptr";
    throw std::logic_error("LLVM backend does not support type '" +
                           cpp_type(type) + "'");
}

std::string llvm_string(const std::string& value) {
    std::ostringstream out;
    for (unsigned char byte : value) {
        if (byte >= 32 && byte < 127 && byte != '\\' && byte != '"') {
            out << static_cast<char>(byte);
        } else {
            out << '\\' << std::hex << std::uppercase
                << std::setw(2) << std::setfill('0')
                << static_cast<unsigned>(byte) << std::dec;
        }
    }
    return out.str();
}

std::string llvm_double(double value) {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "unexpected double size");
    std::memcpy(&bits, &value, sizeof(bits));
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << std::setw(16)
        << std::setfill('0') << bits;
    return out.str();
}

class Generator {
public:
    std::string generate(const Program& program) {
        for (const auto& function : program.functions) {
            if (!function.owner_type.empty()) unsupported("methods");
            functions_.emplace("sun_fn_" + function.name, &function);
        }
        std::vector<std::string> function_ir;
        for (const auto& function : program.functions) {
            function_ir.push_back(emit_function(function));
        }

        reset_function_state();
        predeclare_block(program.main_body);
        emit_allocas();
        emit_block(program.main_body);
        if (!terminated_) out_ << "  ret i32 0\n";
        const auto main_ir = out_.str();

        std::ostringstream module;
        for (const auto& [name, value] : strings_) {
            module << name << " = private unnamed_addr constant ["
                   << value.size() + 1 << " x i8] c\""
                   << llvm_string(value) << "\\00\"\n";
        }
        module << "@.sun.fmt.int = private unnamed_addr constant [6 x i8] c\"%lld\\0A\\00\"\n"
                  "@.sun.fmt.int.no_newline = private unnamed_addr constant [5 x i8] c\"%lld\\00\"\n"
                  "@.sun.fmt.number = private unnamed_addr constant [4 x i8] c\"%g\\0A\\00\"\n"
                  "@.sun.fmt.number.no_newline = private unnamed_addr constant [3 x i8] c\"%g\\00\"\n"
                  "@.sun.fmt.string = private unnamed_addr constant [3 x i8] c\"%s\\00\"\n"
                  "@.sun.fmt.comma = private unnamed_addr constant [3 x i8] c\", \\00\"\n"
                  "@.sun.bool.true = private unnamed_addr constant [5 x i8] c\"true\\00\"\n"
                  "@.sun.bool.false = private unnamed_addr constant [6 x i8] c\"false\\00\"\n"
                  "@.sun.fmt.int.raw = private unnamed_addr constant [5 x i8] c\"%lld\\00\"\n"
                  "@.sun.fmt.number.raw = private unnamed_addr constant [3 x i8] c\"%g\\00\"\n"
                  "@stdout = external global ptr\n\n"
                  "declare i32 @printf(ptr, ...)\n"
                  "declare i32 @puts(ptr)\n"
                  "declare i64 @fwrite(ptr, i64, i64, ptr)\n"
                  "declare i32 @memcmp(ptr, ptr, i64)\n"
                  "declare ptr @malloc(i64)\n"
                  "declare i32 @snprintf(ptr, i64, ptr, ...)\n"
                  "declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)\n"
                  "declare double @llvm.floor.f64(double)\n"
                  "declare i32 @putchar(i32)\n"
                  "declare ptr @sun_llvm_list_create_i64()\n"
                  "declare ptr @sun_llvm_list_create_double()\n"
                  "declare ptr @sun_llvm_list_create_bool()\n"
                  "declare ptr @sun_llvm_list_create_string()\n"
                  "declare void @sun_llvm_list_append_i64(ptr, i64)\n"
                  "declare void @sun_llvm_list_append_double(ptr, double)\n"
                  "declare void @sun_llvm_list_append_bool(ptr, i1)\n"
                  "declare void @sun_llvm_list_append_string(ptr, ptr, i64)\n"
                  "declare i64 @sun_llvm_list_at_i64(ptr, double)\n"
                  "declare double @sun_llvm_list_at_double(ptr, double)\n"
                  "declare i1 @sun_llvm_list_at_bool(ptr, double)\n"
                  "declare ptr @sun_llvm_list_at_string(ptr, double, ptr)\n"
                  "declare i64 @sun_llvm_list_len_i64(ptr)\n"
                  "declare i64 @sun_llvm_list_len_double(ptr)\n"
                  "declare i64 @sun_llvm_list_len_bool(ptr)\n"
                  "declare i64 @sun_llvm_list_len_string(ptr)\n"
                  "declare void @sun_llvm_list_clear_i64(ptr)\n"
                  "declare void @sun_llvm_list_clear_double(ptr)\n"
                  "declare void @sun_llvm_list_clear_bool(ptr)\n"
                  "declare void @sun_llvm_list_clear_string(ptr)\n"
                  "declare i1 @sun_llvm_list_contains_i64(ptr, i64)\n"
                  "declare i1 @sun_llvm_list_contains_double(ptr, double)\n"
                  "declare i1 @sun_llvm_list_contains_bool(ptr, i1)\n"
                  "declare i1 @sun_llvm_list_contains_string(ptr, ptr, i64)\n"
                  "declare void @sun_llvm_list_insert_i64(ptr, double, i64)\n"
                  "declare void @sun_llvm_list_insert_double(ptr, double, double)\n"
                  "declare void @sun_llvm_list_insert_bool(ptr, double, i1)\n"
                  "declare void @sun_llvm_list_insert_string(ptr, double, ptr, i64)\n"
                  "declare i1 @sun_llvm_list_remove_i64(ptr, i64)\n"
                  "declare i1 @sun_llvm_list_remove_double(ptr, double)\n"
                  "declare i1 @sun_llvm_list_remove_bool(ptr, i1)\n"
                  "declare i1 @sun_llvm_list_remove_string(ptr, ptr, i64)\n"
                  "declare i64 @sun_llvm_list_pop_i64(ptr, double)\n"
                  "declare double @sun_llvm_list_pop_double(ptr, double)\n"
                  "declare i1 @sun_llvm_list_pop_bool(ptr, double)\n"
                  "declare ptr @sun_llvm_list_pop_string(ptr, double, ptr)\n"
                  "declare void @sun_llvm_list_set_i64(ptr, double, i64)\n"
                  "declare void @sun_llvm_list_set_double(ptr, double, double)\n"
                  "declare void @sun_llvm_list_set_bool(ptr, double, i1)\n"
                  "declare void @sun_llvm_list_set_string(ptr, double, ptr, i64)\n"
                  "declare void @llvm.trap()\n"
                  "declare { i64, i1 } @llvm.sadd.with.overflow.i64(i64, i64)\n"
                  "declare { i64, i1 } @llvm.ssub.with.overflow.i64(i64, i64)\n"
                  "declare { i64, i1 } @llvm.smul.with.overflow.i64(i64, i64)\n\n"
               << checked_integer_helpers()
               << checked_number_helpers()
               << checked_character_helper();
        for (const auto& function : function_ir) module << '\n' << function;
        module << "\ndefine i32 @main() {\nentry:\n" << main_ir;
        module << "}\n";
        return module.str();
    }

private:
    void reset_function_state() {
        out_.str(std::string());
        out_.clear();
        variables_.clear();
        loop_targets_.clear();
        next_temp_ = 0;
        next_label_ = 0;
        terminated_ = false;
    }

    void emit_allocas() {
        for (const auto& [name, type] : variables_) {
            out_ << "  %sun.slot." << name << " = alloca " << llvm_type(type) << '\n';
            if (is_string_like(type)) {
                out_ << "  %sun.len." << name << " = alloca i64\n";
            }
        }
        if (!variables_.empty()) out_ << '\n';
    }

    std::string emit_function(const Function& function) {
        reset_function_state();
        for (const auto& parameter : function.parameters) {
            variables_.emplace("sun_arg_" + parameter.name, parameter.type);
        }
        predeclare_block(function.body);
        std::ostringstream signature;
        signature << "define "
                  << (function.return_type ? llvm_type(*function.return_type) : "void")
                  << " @sun_fn_" << function.name << '(';
        bool first = true;
        for (const auto& parameter : function.parameters) {
            if (!first) signature << ", ";
            first = false;
            const auto name = "%sun_arg_" + parameter.name;
            signature << llvm_type(parameter.type) << ' ' << name;
            if (is_string_like(parameter.type)) {
                signature << ", i64 " << name << ".len";
            }
        }
        if (function.return_type && is_string_like(*function.return_type)) {
            if (!first) signature << ", ";
            signature << "ptr %sun.return.len";
        }
        signature << ") {\nentry:\n";
        emit_allocas();
        for (const auto& parameter : function.parameters) {
            const auto name = "sun_arg_" + parameter.name;
            out_ << "  store " << llvm_type(parameter.type) << " %" << name
                 << ", ptr %sun.slot." << name << '\n';
            if (is_string_like(parameter.type)) {
                out_ << "  store i64 %" << name << ".len, ptr %sun.len."
                     << name << '\n';
            }
        }
        emit_block(function.body);
        if (!terminated_) out_ << "  ret void\n";
        return signature.str() + out_.str() + "}\n";
    }

    void emit_call_arguments(std::ostringstream& call,
                             const std::vector<Expression>& arguments,
                             const Function& function) {
        bool first = true;
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            const auto value = emit_expression(arguments[i]);
            if (!first) call << ", ";
            first = false;
            call << llvm_type(value.type) << ' ' << value.operand;
            if (is_string_like(function.parameters.at(i).type)) {
                call << ", i64 " << value.string_length;
            }
        }
    }

    std::string checked_integer_helpers() const {
        return R"ir(define internal i64 @sun_checked_add(i64 %a, i64 %b) {
entry:
  %pair = call { i64, i1 } @llvm.sadd.with.overflow.i64(i64 %a, i64 %b)
  %value = extractvalue { i64, i1 } %pair, 0
  %overflow = extractvalue { i64, i1 } %pair, 1
  br i1 %overflow, label %fail, label %ok
fail:
  call void @llvm.trap()
  unreachable
ok:
  ret i64 %value
}

define internal i64 @sun_checked_sub(i64 %a, i64 %b) {
entry:
  %pair = call { i64, i1 } @llvm.ssub.with.overflow.i64(i64 %a, i64 %b)
  %value = extractvalue { i64, i1 } %pair, 0
  %overflow = extractvalue { i64, i1 } %pair, 1
  br i1 %overflow, label %fail, label %ok
fail:
  call void @llvm.trap()
  unreachable
ok:
  ret i64 %value
}

define internal i64 @sun_checked_mul(i64 %a, i64 %b) {
entry:
  %pair = call { i64, i1 } @llvm.smul.with.overflow.i64(i64 %a, i64 %b)
  %value = extractvalue { i64, i1 } %pair, 0
  %overflow = extractvalue { i64, i1 } %pair, 1
  br i1 %overflow, label %fail, label %ok
fail:
  call void @llvm.trap()
  unreachable
ok:
  ret i64 %value
}

define internal i64 @sun_checked_div(i64 %a, i64 %b) {
entry:
  %zero = icmp eq i64 %b, 0
  %min = icmp eq i64 %a, -9223372036854775808
  %negone = icmp eq i64 %b, -1
  %overflow = and i1 %min, %negone
  %invalid = or i1 %zero, %overflow
  br i1 %invalid, label %fail, label %ok
fail:
  call void @llvm.trap()
  unreachable
ok:
  %value = sdiv i64 %a, %b
  ret i64 %value
}

define internal i64 @sun_checked_mod(i64 %a, i64 %b) {
entry:
  %zero = icmp eq i64 %b, 0
  %min = icmp eq i64 %a, -9223372036854775808
  %negone = icmp eq i64 %b, -1
  %overflow = and i1 %min, %negone
  %invalid = or i1 %zero, %overflow
  br i1 %invalid, label %fail, label %ok
fail:
  call void @llvm.trap()
  unreachable
ok:
  %value = srem i64 %a, %b
  ret i64 %value
}

define internal i64 @sun_checked_neg(i64 %a) {
entry:
  %pair = call { i64, i1 } @llvm.ssub.with.overflow.i64(i64 0, i64 %a)
  %value = extractvalue { i64, i1 } %pair, 0
  %overflow = extractvalue { i64, i1 } %pair, 1
  br i1 %overflow, label %fail, label %ok
fail:
  call void @llvm.trap()
  unreachable
ok:
  ret i64 %value
}
)ir";
    }

        std::string checked_number_helpers() const {
            return R"ir(define internal i64 @sun_checked_number_to_int(double %value) {
entry:
  %finite = fcmp ord double %value, %value
  br i1 %finite, label %whole.check, label %fail
whole.check:
  %floor = call double @llvm.floor.f64(double %value)
  %whole = fcmp oeq double %value, %floor
  br i1 %whole, label %range.check, label %fail
range.check:
  %limit = fcmp oge double %value, 0xC3E0000000000000
  %upper = fcmp olt double %value, 0x43E0000000000000
  %range.valid = and i1 %limit, %upper
  br i1 %range.valid, label %convert, label %fail
convert:
  %integer = fptosi double %value to i64
  ret i64 %integer
fail:
  call void @llvm.trap()
  unreachable
}
)ir";
        }

        std::string checked_character_helper() const {
                return R"ir(define internal ptr @sun_checked_char(ptr %value, i64 %length) {
entry:
    %is.one = icmp eq i64 %length, 1
    br i1 %is.one, label %one, label %check.two
check.two:
    %is.two = icmp eq i64 %length, 2
    br i1 %is.two, label %two, label %check.three
check.three:
    %is.three = icmp eq i64 %length, 3
    br i1 %is.three, label %three, label %check.four
check.four:
    %is.four = icmp eq i64 %length, 4
    br i1 %is.four, label %four, label %fail
one:
    %one.byte = load i8, ptr %value
    %one.code = zext i8 %one.byte to i32
    %one.valid = icmp ult i32 %one.code, 128
    br i1 %one.valid, label %valid, label %fail
two:
    %two.byte0 = load i8, ptr %value
    %two.code0 = zext i8 %two.byte0 to i32
    %two.byte1.ptr = getelementptr i8, ptr %value, i64 1
    %two.byte1 = load i8, ptr %two.byte1.ptr
    %two.code1 = zext i8 %two.byte1 to i32
    %two.low = icmp uge i32 %two.code0, 194
    %two.high = icmp ule i32 %two.code0, 223
    %two.lead.valid = and i1 %two.low, %two.high
    %two.cont.low = icmp uge i32 %two.code1, 128
    %two.cont.high = icmp ule i32 %two.code1, 191
    %two.cont.valid = and i1 %two.cont.low, %two.cont.high
    %two.valid = and i1 %two.lead.valid, %two.cont.valid
    br i1 %two.valid, label %valid, label %fail
three:
    %three.byte0 = load i8, ptr %value
    %three.code0 = zext i8 %three.byte0 to i32
    %three.byte1.ptr = getelementptr i8, ptr %value, i64 1
    %three.byte1 = load i8, ptr %three.byte1.ptr
    %three.code1 = zext i8 %three.byte1 to i32
    %three.byte2.ptr = getelementptr i8, ptr %value, i64 2
    %three.byte2 = load i8, ptr %three.byte2.ptr
    %three.code2 = zext i8 %three.byte2 to i32
    %three.low = icmp uge i32 %three.code0, 224
    %three.high = icmp ule i32 %three.code0, 239
    %three.lead.valid = and i1 %three.low, %three.high
    %three.cont1.low = icmp uge i32 %three.code1, 128
    %three.cont1.high = icmp ule i32 %three.code1, 191
    %three.cont1.valid = and i1 %three.cont1.low, %three.cont1.high
    %three.cont2.low = icmp uge i32 %three.code2, 128
    %three.cont2.high = icmp ule i32 %three.code2, 191
    %three.cont2.valid = and i1 %three.cont2.low, %three.cont2.high
    %three.is.e0 = icmp eq i32 %three.code0, 224
    %three.not.e0 = icmp ne i32 %three.code0, 224
    %three.e0.low = icmp uge i32 %three.code1, 160
    %three.e0.valid = or i1 %three.not.e0, %three.e0.low
    %three.is.ed = icmp eq i32 %three.code0, 237
    %three.not.ed = icmp ne i32 %three.code0, 237
    %three.ed.high = icmp ule i32 %three.code1, 159
    %three.ed.valid = or i1 %three.not.ed, %three.ed.high
    %three.valid0 = and i1 %three.lead.valid, %three.cont1.valid
    %three.valid1 = and i1 %three.cont2.valid, %three.e0.valid
    %three.valid2 = and i1 %three.ed.valid, %three.valid0
    %three.valid = and i1 %three.valid1, %three.valid2
    br i1 %three.valid, label %valid, label %fail
four:
    %four.byte0 = load i8, ptr %value
    %four.code0 = zext i8 %four.byte0 to i32
    %four.byte1.ptr = getelementptr i8, ptr %value, i64 1
    %four.byte1 = load i8, ptr %four.byte1.ptr
    %four.code1 = zext i8 %four.byte1 to i32
    %four.byte2.ptr = getelementptr i8, ptr %value, i64 2
    %four.byte2 = load i8, ptr %four.byte2.ptr
    %four.code2 = zext i8 %four.byte2 to i32
    %four.byte3.ptr = getelementptr i8, ptr %value, i64 3
    %four.byte3 = load i8, ptr %four.byte3.ptr
    %four.code3 = zext i8 %four.byte3 to i32
    %four.low = icmp uge i32 %four.code0, 240
    %four.high = icmp ule i32 %four.code0, 244
    %four.lead.valid = and i1 %four.low, %four.high
    %four.cont1.low = icmp uge i32 %four.code1, 128
    %four.cont1.high = icmp ule i32 %four.code1, 191
    %four.cont1.valid = and i1 %four.cont1.low, %four.cont1.high
    %four.cont2.low = icmp uge i32 %four.code2, 128
    %four.cont2.high = icmp ule i32 %four.code2, 191
    %four.cont2.valid = and i1 %four.cont2.low, %four.cont2.high
    %four.cont3.low = icmp uge i32 %four.code3, 128
    %four.cont3.high = icmp ule i32 %four.code3, 191
    %four.cont3.valid = and i1 %four.cont3.low, %four.cont3.high
    %four.is.f0 = icmp eq i32 %four.code0, 240
    %four.not.f0 = icmp ne i32 %four.code0, 240
    %four.f0.low = icmp uge i32 %four.code1, 144
    %four.f0.valid = or i1 %four.not.f0, %four.f0.low
    %four.is.f4 = icmp eq i32 %four.code0, 244
    %four.not.f4 = icmp ne i32 %four.code0, 244
    %four.f4.high = icmp ule i32 %four.code1, 143
    %four.f4.valid = or i1 %four.not.f4, %four.f4.high
    %four.valid0 = and i1 %four.lead.valid, %four.cont1.valid
    %four.valid1 = and i1 %four.cont2.valid, %four.cont3.valid
    %four.valid2 = and i1 %four.f0.valid, %four.f4.valid
    %four.valid3 = and i1 %four.valid0, %four.valid1
    %four.valid = and i1 %four.valid2, %four.valid3
    br i1 %four.valid, label %valid, label %fail
fail:
    call void @llvm.trap()
    unreachable
valid:
    ret ptr %value
}
)ir";
        }

    [[noreturn]] void unsupported(const std::string& feature) const {
        throw std::logic_error("LLVM backend does not yet support " + feature);
    }

    std::string temporary() { return "%sun.tmp." + std::to_string(next_temp_++); }
    std::string label(const std::string& prefix) {
        return "sun." + prefix + "." + std::to_string(next_label_++);
    }

    std::string string_pointer(const std::string& value) {
        for (const auto& entry : strings_) {
            if (entry.second == value) return entry.first;
        }
        const auto name = "@.sun.str." + std::to_string(strings_.size());
        strings_.emplace_back(name, value);
        return name;
    }

    Value emit_expression(const Expression& expression) {
        if (expression.kind == Expression::Kind::literal) {
            if (expression.type == Type::boolean) {
                return {Type::boolean, expression.value == "true" ? "true" : "false"};
            }
            if (expression.type == Type::integer) {
                return {Type::integer, expression.value};
            }
            if (expression.type == Type::number) {
                std::size_t parsed = 0;
                const auto number = std::stod(expression.value, &parsed);
                if (parsed != expression.value.size() || !std::isfinite(number)) {
                    unsupported("non-finite numeric literals");
                }
                return {Type::number, llvm_double(number)};
            }
            if (expression.type == Type::character) {
                return {Type::character, string_pointer(expression.value),
                        std::to_string(expression.value.size())};
            }
            unsupported("literal type '" + cpp_type(expression.type) + "'");
        }
        if (expression.kind == Expression::Kind::string_literal) {
            return {Type::string, string_pointer(expression.value),
                    std::to_string(expression.value.size())};
        }
        if (expression.kind == Expression::Kind::string_interpolation) {
            const auto value = emit_expression(expression.children.at(0));
            if (is_string_like(value.type)) return {Type::string, value.operand,
                                                   value.string_length};
            if (value.type == Type::integer) {
                const auto buffer = temporary();
                out_ << "  " << buffer << " = call ptr @malloc(i64 32)\n";
                const auto length = temporary();
                out_ << "  " << length << " = call i32 (ptr, i64, ptr, ...) "
                     << "@snprintf(ptr " << buffer << ", i64 32, ptr @.sun.fmt.int.raw, i64 "
                     << value.operand << ")\n";
                const auto length64 = temporary();
                out_ << "  " << length64 << " = sext i32 " << length << " to i64\n";
                return {Type::string, buffer, length64};
            }
            if (value.type == Type::number) {
                const auto buffer = temporary();
                out_ << "  " << buffer << " = call ptr @malloc(i64 64)\n";
                const auto length = temporary();
                out_ << "  " << length << " = call i32 (ptr, i64, ptr, ...) "
                     << "@snprintf(ptr " << buffer << ", i64 64, ptr @.sun.fmt.number.raw, double "
                     << value.operand << ")\n";
                const auto length64 = temporary();
                out_ << "  " << length64 << " = sext i32 " << length << " to i64\n";
                return {Type::string, buffer, length64};
            }
            if (value.type == Type::boolean) {
                const auto text = temporary();
                const auto length = temporary();
                out_ << "  " << text << " = select i1 " << value.operand
                     << ", ptr @.sun.bool.true, ptr @.sun.bool.false\n"
                     << "  " << length << " = select i1 " << value.operand
                     << ", i64 4, i64 5\n";
                return {Type::string, text, length};
            }
            unsupported("string interpolation type '" + cpp_type(value.type) + "'");
        }
        if (expression.kind == Expression::Kind::formatted_string) {
            std::vector<Value> parts;
            std::string total_length = "0";
            for (const auto& child : expression.children) {
                const auto part = emit_expression(child);
                const auto next_length = temporary();
                out_ << "  " << next_length << " = add i64 " << total_length
                     << ", " << part.string_length << '\n';
                total_length = next_length;
                parts.push_back(part);
            }
            const auto buffer = temporary();
            out_ << "  " << buffer << " = call ptr @malloc(i64 " << total_length << ")\n";
            std::string offset = "0";
            for (const auto& part : parts) {
                const auto destination = temporary();
                out_ << "  " << destination << " = getelementptr i8, ptr "
                     << buffer << ", i64 " << offset << '\n'
                     << "  call void @llvm.memcpy.p0.p0.i64(ptr " << destination
                     << ", ptr " << part.operand << ", i64 " << part.string_length
                     << ", i1 false)\n";
                const auto next_offset = temporary();
                out_ << "  " << next_offset << " = add i64 " << offset
                     << ", " << part.string_length << '\n';
                offset = next_offset;
            }
            return {Type::string, buffer, total_length};
        }
        if (expression.kind == Expression::Kind::list) {
            const auto item_type = list_item_type(expression.type);
            const auto suffix = llvm_list_suffix(item_type);
            const auto result = temporary();
            out_ << "  " << result << " = call ptr @sun_llvm_list_create_" << suffix << "()\n";
            for (const auto& child : expression.children) {
                const auto value = emit_expression(child);
                if (is_string_like(item_type)) {
                    out_ << "  call void @sun_llvm_list_append_string(ptr " << result
                         << ", ptr " << value.operand << ", i64 " << value.string_length << ")\n";
                } else {
                    out_ << "  call void @sun_llvm_list_append_" << suffix << "(ptr " << result
                         << ", " << llvm_type(item_type) << " " << value.operand << ")\n";
                }
            }
            return {expression.type, result};
        }
        if (expression.kind == Expression::Kind::variable) {
            const auto found = variables_.find(expression.value);
            if (found == variables_.end()) {
                unsupported("variable '" + expression.value + "'");
            }
            const auto result = temporary();
            out_ << "  " << result << " = load " << llvm_type(found->second)
                 << ", ptr %sun.slot." << expression.value << '\n';
            if (is_string_like(found->second)) {
                const auto length = temporary();
                out_ << "  " << length << " = load i64, ptr %sun.len."
                     << expression.value << '\n';
                return {found->second, result, length};
            }
            return {found->second, result};
        }
        if (expression.kind == Expression::Kind::cast) {
            const auto value = emit_expression(expression.children.at(0));
            if (expression.value == "double" && value.type == Type::integer) {
                const auto result = temporary();
                out_ << "  " << result << " = sitofp i64 " << value.operand
                     << " to double\n";
                return {Type::number, result};
            }
            if (expression.value == "std::int64_t" && value.type == Type::integer) {
                return {Type::integer, value.operand};
            }
            if (expression.value == "std::int64_t" && value.type == Type::number) {
                const auto result = temporary();
                out_ << "  " << result << " = call i64 @sun_checked_number_to_int(double "
                     << value.operand << ")\n";
                return {Type::integer, result};
            }
            unsupported("cast to '" + expression.value + "'");
        }
        if (expression.kind == Expression::Kind::member_call) {
            if (expression.value == "size" && expression.children.size() == 1) {
                const auto receiver = emit_expression(expression.children[0]);
                if (is_list_type(expression.children[0].type)) {
                    const auto suffix = llvm_list_suffix(
                        list_item_type(expression.children[0].type));
                    const auto result = temporary();
                    out_ << "  " << result << " = call i64 @sun_llvm_list_len_" << suffix
                         << "(ptr " << receiver.operand << ")\n";
                    return {Type::integer, result};
                }
                if (expression.children[0].type == Type::string) {
                    return {Type::integer, receiver.string_length};
                }
            }
            unsupported("member call '" + expression.value + "'");
        }
        if (expression.kind == Expression::Kind::unary) {
            const auto value = emit_expression(expression.children.at(0));
            const auto result = temporary();
            if (expression.value == "!" && value.type == Type::boolean) {
                out_ << "  " << result << " = xor i1 " << value.operand
                     << ", true\n";
                return {Type::boolean, result};
            }
            if (expression.value == "-" && value.type == Type::number) {
                out_ << "  " << result << " = fneg double " << value.operand << '\n';
                return {Type::number, result};
            }
            if (expression.value == "sun_int_neg" && value.type == Type::integer) {
                out_ << "  " << result << " = call i64 @sun_checked_neg(i64 "
                     << value.operand << ")\n";
                return {Type::integer, result};
            }
            unsupported("unary operator '" + expression.value + "'");
        }
        if (expression.kind == Expression::Kind::binary) {
            return emit_binary(expression);
        }
        if (expression.kind == Expression::Kind::call) {
            const auto function = functions_.find(expression.value);
            if (function != functions_.end()) {
                std::ostringstream arguments;
                emit_call_arguments(arguments, expression.children, *function->second);
                const auto result = temporary();
                auto argument_text = arguments.str();
                std::string length_slot;
                if (function->second->return_type &&
                    is_string_like(*function->second->return_type)) {
                    length_slot = temporary();
                    out_ << "  " << length_slot << " = alloca i64\n";
                    if (!argument_text.empty()) argument_text += ", ";
                    argument_text += "ptr " + length_slot;
                }
                out_ << "  " << result << " = call " << llvm_type(expression.type)
                     << " @" << expression.value << '(' << argument_text << ")\n";
                if (is_string_like(expression.type)) {
                    const auto length = temporary();
                    out_ << "  " << length << " = load i64, ptr " << length_slot << '\n';
                    return {expression.type, result, length};
                }
                return {expression.type, result};
            }
            if (expression.value == "sun_list_at" && expression.children.size() == 2) {
                const auto list = emit_expression(expression.children[0]);
                auto index = emit_expression(expression.children[1]);
                if (index.type == Type::integer) {
                    const auto converted = temporary();
                    out_ << "  " << converted << " = sitofp i64 " << index.operand << " to double\n";
                    index = {Type::number, converted};
                }
                const auto suffix = llvm_list_suffix(expression.type);
                if (is_string_like(expression.type)) {
                    const auto length_slot = temporary();
                    out_ << "  " << length_slot << " = alloca i64\n";
                    const auto result = temporary();
                    out_ << "  " << result << " = call ptr @sun_llvm_list_at_string(ptr "
                         << list.operand << ", double " << index.operand << ", ptr "
                         << length_slot << ")\n";
                    const auto length = temporary();
                    out_ << "  " << length << " = load i64, ptr " << length_slot << '\n';
                    return {expression.type, result, length};
                }
                const auto result = temporary();
                out_ << "  " << result << " = call " << llvm_type(expression.type)
                     << " @sun_llvm_list_at_" << suffix << "(ptr " << list.operand
                     << ", double " << index.operand << ")\n";
                return {expression.type, result};
            }
            if ((expression.value == "sun_list_contains" ||
                 expression.value == "sun_list_remove" ||
                 expression.value == "sun_list_pop") &&
                !expression.children.empty()) {
                const auto list = emit_expression(expression.children[0]);
                if (expression.value == "sun_list_contains") {
                    const auto item = emit_expression(expression.children[1]);
                    const auto suffix = llvm_list_suffix(item.type);
                    const auto result = temporary();
                    if (is_string_like(item.type)) {
                        out_ << "  " << result << " = call i1 @sun_llvm_list_contains_string(ptr "
                             << list.operand << ", ptr " << item.operand << ", i64 "
                             << item.string_length << ")\n";
                    } else {
                        out_ << "  " << result << " = call i1 @sun_llvm_list_contains_" << suffix
                             << "(ptr " << list.operand << ", " << llvm_type(item.type) << " "
                             << item.operand << ")\n";
                    }
                    return {Type::boolean, result};
                }
                if (expression.value == "sun_list_remove") {
                    const auto item = emit_expression(expression.children[1]);
                    const auto suffix = llvm_list_suffix(item.type);
                    const auto result = temporary();
                    if (is_string_like(item.type)) {
                        out_ << "  " << result << " = call i1 @sun_llvm_list_remove_string(ptr "
                             << list.operand << ", ptr " << item.operand << ", i64 "
                             << item.string_length << ")\n";
                    } else {
                        out_ << "  " << result << " = call i1 @sun_llvm_list_remove_" << suffix
                             << "(ptr " << list.operand << ", " << llvm_type(item.type) << " "
                             << item.operand << ")\n";
                    }
                    return {Type::boolean, result};
                }
                auto index = emit_expression(expression.children[1]);
                if (index.type == Type::integer) {
                    const auto converted = temporary();
                    out_ << "  " << converted << " = sitofp i64 " << index.operand << " to double\n";
                    index = {Type::number, converted};
                }
                const auto suffix = llvm_list_suffix(expression.type);
                if (is_string_like(expression.type)) {
                    const auto length_slot = temporary();
                    out_ << "  " << length_slot << " = alloca i64\n";
                    const auto result = temporary();
                    out_ << "  " << result << " = call ptr @sun_llvm_list_pop_string(ptr "
                         << list.operand << ", double " << index.operand << ", ptr "
                         << length_slot << ")\n";
                    const auto length = temporary();
                    out_ << "  " << length << " = load i64, ptr " << length_slot << '\n';
                    return {expression.type, result, length};
                }
                const auto result = temporary();
                out_ << "  " << result << " = call " << llvm_type(expression.type)
                     << " @sun_llvm_list_pop_" << suffix << "(ptr " << list.operand
                     << ", double " << index.operand << ")\n";
                return {expression.type, result};
            }
            if (expression.kind == Expression::Kind::member_call &&
                expression.value == "size" && expression.children.size() == 1) {
                const auto receiver = emit_expression(expression.children[0]);
                const auto receiver_type = expression.children[0].type;
                if (is_list_type(receiver_type)) {
                    const auto suffix = llvm_list_suffix(list_item_type(receiver_type));
                    const auto result = temporary();
                    out_ << "  " << result << " = call i64 @sun_llvm_list_len_" << suffix
                         << "(ptr " << receiver.operand << ")\n";
                    return {Type::integer, result};
                }
            }
            if (expression.value == "sun_to_char" && expression.children.size() == 1) {
                const auto value = emit_expression(expression.children.front());
                const auto result = temporary();
                out_ << "  " << result << " = call ptr @sun_checked_char(ptr "
                     << value.operand << ", i64 " << value.string_length << ")\n";
                return {Type::character, result, value.string_length};
            }
            if (expression.value == "sun_to_int" && expression.children.size() == 1) {
                const auto value = emit_expression(expression.children[0]);
                if (value.type == Type::integer) {
                    return {Type::integer, value.operand};
                }
                if (value.type == Type::number) {
                    const auto result = temporary();
                    out_ << "  " << result << " = call i64 @sun_checked_number_to_int(double "
                         << value.operand << ")\n";
                    return {Type::integer, result};
                }
                unsupported("int conversion source type '" + cpp_type(value.type) + "'");
            }
            if (expression.value == "sun_int_add" ||
                expression.value == "sun_int_sub" ||
                expression.value == "sun_int_mul" ||
                expression.value == "sun_int_div" ||
                expression.value == "sun_int_mod") {
                const auto left = emit_expression(expression.children.at(0));
                const auto right = emit_expression(expression.children.at(1));
                const auto helper = expression.value == "sun_int_add" ? "sun_checked_add"
                                  : expression.value == "sun_int_sub" ? "sun_checked_sub"
                                  : expression.value == "sun_int_mul" ? "sun_checked_mul"
                                  : expression.value == "sun_int_div" ? "sun_checked_div"
                                                                       : "sun_checked_mod";
                const auto result = temporary();
                out_ << "  " << result << " = call i64 @" << helper << "(i64 "
                     << left.operand << ", i64 " << right.operand << ")\n";
                return {Type::integer, result};
            }
            unsupported("call to '" + expression.value + "'");
        }
        unsupported("expression node");
    }

    Value emit_binary(const Expression& expression) {
        if (expression.type == Type::boolean &&
            (expression.value == "&&" || expression.value == "||")) {
            const auto left = emit_expression(expression.children.at(0));
            const auto short_label = label("bool.short");
            const auto right_label = label("bool.right");
            const auto end_label = label("bool.end");
            const auto short_value = expression.value == "&&" ? "false" : "true";
            out_ << "  br i1 " << left.operand << ", label %"
                 << (expression.value == "&&" ? right_label : short_label)
                 << ", label %"
                 << (expression.value == "&&" ? short_label : right_label) << '\n'
                 << short_label << ":\n"
                 << "  br label %" << end_label << '\n'
                 << right_label << ":\n";
            const auto right = emit_expression(expression.children.at(1));
            out_ << "  br label %" << end_label << '\n'
                 << end_label << ":\n";
            const auto merged = temporary();
            out_ << "  " << merged << " = phi i1 [" << short_value
                 << ", %" << short_label << "], [" << right.operand
                 << ", %" << right_label << "]\n";
            return {Type::boolean, merged};
        }
        const auto left = emit_expression(expression.children.at(0));
        const auto right = emit_expression(expression.children.at(1));
        const auto result = temporary();
        if (expression.type == Type::number) {
            const auto op = expression.value == "+" ? "fadd"
                          : expression.value == "-" ? "fsub"
                          : expression.value == "*" ? "fmul"
                          : expression.value == "/" ? "fdiv" : nullptr;
            if (!op) unsupported("number operator '" + expression.value + "'");
            out_ << "  " << result << " = " << op << " double "
                 << left.operand << ", " << right.operand << '\n';
            return {Type::number, result};
        }
        if (expression.type == Type::boolean) {
            if (expression.value == "&&" || expression.value == "||") {
                const auto short_label = label("bool.short");
                const auto right_label = label("bool.right");
                const auto end_label = label("bool.end");
                const auto short_value = expression.value == "&&" ? "false" : "true";
                out_ << "  br i1 " << left.operand << ", label %"
                     << (expression.value == "&&" ? right_label : short_label)
                     << ", label %"
                     << (expression.value == "&&" ? short_label : right_label) << '\n'
                     << short_label << ":\n"
                     << "  br label %" << end_label << '\n'
                     << right_label << ":\n";
                const auto right = emit_expression(expression.children.at(1));
                out_ << "  br label %" << end_label << '\n'
                     << end_label << ":\n";
                const auto merged = temporary();
                out_ << "  " << merged << " = phi i1 [" << short_value
                     << ", %" << short_label << "], [" << right.operand
                     << ", %" << right_label << "]\n";
                return {Type::boolean, merged};
            } else if (left.type == Type::number) {
                const auto predicate = expression.value == "==" ? "oeq"
                                     : expression.value == "!=" ? "une"
                                     : expression.value == "<" ? "olt"
                                     : expression.value == "<=" ? "ole"
                                     : expression.value == ">" ? "ogt" : "oge";
                out_ << "  " << result << " = fcmp " << predicate << " double "
                     << left.operand << ", " << right.operand << '\n';
            } else if (left.type == Type::integer || left.type == Type::boolean) {
                const auto predicate = expression.value == "==" ? "eq"
                                     : expression.value == "!=" ? "ne"
                                     : expression.value == "<" ? "slt"
                                     : expression.value == "<=" ? "sle"
                                     : expression.value == ">" ? "sgt" : "sge";
                out_ << "  " << result << " = icmp " << predicate << ' '
                     << llvm_type(left.type) << ' ' << left.operand << ", "
                     << right.operand << '\n';
            } else if (left.type == Type::character &&
                       (expression.value == "==" || expression.value == "!=")) {
                const auto same_length = temporary();
                const auto compare_length = temporary();
                const auto compared = temporary();
                const auto same_bytes = temporary();
                const auto equal = temporary();
                out_ << "  " << same_length << " = icmp eq i64 "
                     << left.string_length << ", " << right.string_length << '\n'
                     << "  " << compare_length << " = select i1 " << same_length
                     << ", i64 " << left.string_length << ", i64 0\n"
                     << "  " << compared << " = call i32 @memcmp(ptr "
                     << left.operand << ", ptr " << right.operand << ", i64 "
                     << compare_length << ")\n"
                     << "  " << same_bytes << " = icmp eq i32 " << compared
                     << ", 0\n"
                     << "  " << equal << " = and i1 " << same_length << ", "
                     << same_bytes << '\n';
                if (expression.value == "==") {
                    out_ << "  " << result << " = or i1 " << equal << ", false\n";
                } else {
                    out_ << "  " << result << " = xor i1 " << equal << ", true\n";
                }
            } else if (left.type == Type::string &&
                       (expression.value == "==" || expression.value == "!=")) {
                const auto same_length = temporary();
                const auto compare_length = temporary();
                const auto compared = temporary();
                const auto same_bytes = temporary();
                const auto equal = temporary();
                out_ << "  " << same_length << " = icmp eq i64 "
                     << left.string_length << ", " << right.string_length << '\n'
                     << "  " << compare_length << " = select i1 " << same_length
                     << ", i64 " << left.string_length << ", i64 0\n"
                     << "  " << compared << " = call i32 @memcmp(ptr "
                     << left.operand << ", ptr " << right.operand << ", i64 "
                     << compare_length << ")\n"
                     << "  " << same_bytes << " = icmp eq i32 " << compared
                     << ", 0\n"
                     << "  " << equal << " = and i1 " << same_length << ", "
                     << same_bytes << '\n';
                if (expression.value == "==") {
                    out_ << "  " << result << " = or i1 " << equal << ", false\n";
                } else {
                    out_ << "  " << result << " = xor i1 " << equal << ", true\n";
                }
            } else {
                unsupported("string comparison");
            }
            return {Type::boolean, result};
        }
        unsupported("binary expression result type '" + cpp_type(expression.type) + "'");
    }

    void emit_print(const PrintStatement& print) {
        const auto value = emit_expression(print.value);
        if (is_list_type(value.type)) {
            emit_print_list(value);
        } else if (is_string_like(value.type)) {
            const auto stream = temporary();
            const auto ignored = temporary();
            out_ << "  " << stream << " = load ptr, ptr @stdout\n"
                 << "  " << ignored << " = call i64 @fwrite(ptr " << value.operand
                 << ", i64 " << value.string_length
                 << ", i64 1, ptr " << stream << ")\n"
                 << "  call i32 @putchar(i32 10)\n";
        } else if (value.type == Type::boolean) {
            const auto text = temporary();
            out_ << "  " << text << " = select i1 " << value.operand
                 << ", ptr @.sun.bool.true, ptr @.sun.bool.false\n"
                 << "  call i32 @puts(ptr " << text << ")\n";
        } else if (value.type == Type::integer) {
            out_ << "  call i32 (ptr, ...) @printf(ptr @.sun.fmt.int, i64 "
                 << value.operand << ")\n";
        } else if (value.type == Type::number) {
            out_ << "  call i32 (ptr, ...) @printf(ptr @.sun.fmt.number, double "
                 << value.operand << ")\n";
        } else {
            unsupported("printing type '" + cpp_type(value.type) + "'");
        }
    }

    void emit_print_list(const Value& value) {
        const auto item_type = list_item_type(value.type);
        const auto suffix = llvm_list_suffix(item_type);
        const auto length = temporary();
        out_ << "  " << length << " = call i64 @sun_llvm_list_len_" << suffix
             << "(ptr " << value.operand << ")\n"
             << "  call i32 @putchar(i32 91)\n";

        const auto index_slot = temporary();
        out_ << "  " << index_slot << " = alloca i64\n"
             << "  store i64 0, ptr " << index_slot << '\n';
        const auto condition = label("list.print.cond");
        const auto body = label("list.print.body");
        const auto comma_label = label("list.print.comma");
        const auto item_label = label("list.print.item");
        const auto increment = label("list.print.increment");
        const auto end = label("list.print.end");

        out_ << "  br label %" << condition << '\n'
             << condition << ":\n";
        const auto index = temporary();
        out_ << "  " << index << " = load i64, ptr " << index_slot << '\n';
        const auto more = temporary();
        out_ << "  " << more << " = icmp slt i64 " << index << ", " << length << '\n'
             << "  br i1 " << more << ", label %" << body
             << ", label %" << end << '\n'
             << body << ":\n";
        const auto not_first = temporary();
        out_ << "  " << not_first << " = icmp sgt i64 " << index << ", 0\n"
             << "  br i1 " << not_first << ", label %" << comma_label
             << ", label %" << item_label << '\n'
             << comma_label << ":\n"
             << "  call i32 @printf(ptr @.sun.fmt.comma)\n"
             << "  br label %" << item_label << '\n'
             << item_label << ":\n";

        const auto index_number = temporary();
        out_ << "  " << index_number << " = sitofp i64 " << index << " to double\n";
        if (is_string_like(item_type)) {
            const auto item_length_slot = temporary();
            out_ << "  " << item_length_slot << " = alloca i64\n";
            const auto item = temporary();
            out_ << "  " << item << " = call ptr @sun_llvm_list_at_string(ptr "
                 << value.operand << ", double " << index_number << ", ptr "
                 << item_length_slot << ")\n";
            const auto item_length = temporary();
            const auto stream = temporary();
            const auto ignored = temporary();
            out_ << "  " << item_length << " = load i64, ptr " << item_length_slot << '\n'
                 << "  " << stream << " = load ptr, ptr @stdout\n"
                 << "  " << ignored << " = call i64 @fwrite(ptr " << item
                 << ", i64 " << item_length << ", i64 1, ptr " << stream << ")\n";
        } else if (item_type == Type::boolean) {
            const auto item = temporary();
            const auto text = temporary();
            out_ << "  " << item << " = call i1 @sun_llvm_list_at_bool(ptr "
                 << value.operand << ", double " << index_number << ")\n"
                 << "  " << text << " = select i1 " << item
                 << ", ptr @.sun.bool.true, ptr @.sun.bool.false\n"
                 << "  call i32 (ptr, ...) @printf(ptr @.sun.fmt.string, ptr "
                 << text << ")\n";
        } else if (item_type == Type::integer) {
            const auto item = temporary();
            out_ << "  " << item << " = call i64 @sun_llvm_list_at_i64(ptr "
                 << value.operand << ", double " << index_number << ")\n"
                 << "  call i32 (ptr, ...) @printf(ptr @.sun.fmt.int.no_newline, i64 "
                 << item << ")\n";
        } else if (item_type == Type::number) {
            const auto item = temporary();
            out_ << "  " << item << " = call double @sun_llvm_list_at_double(ptr "
                 << value.operand << ", double " << index_number << ")\n"
                 << "  call i32 (ptr, ...) @printf(ptr @.sun.fmt.number.no_newline, double "
                 << item << ")\n";
        }

        out_ << "  br label %" << increment << '\n'
             << increment << ":\n";
        const auto next_index = temporary();
        out_ << "  " << next_index << " = add i64 " << index << ", 1\n"
             << "  store i64 " << next_index << ", ptr " << index_slot << '\n'
             << "  br label %" << condition << '\n'
             << end << ":\n"
             << "  call i32 @putchar(i32 93)\n"
             << "  call i32 @putchar(i32 10)\n";
    }

    void emit_statement(const Statement& statement) {
        if (const auto* print = std::get_if<PrintStatement>(&statement.node)) {
            emit_print(*print);
        } else if (const auto* declaration =
                       std::get_if<VariableDeclarationStatement>(&statement.node)) {
            const auto slot = "%sun.slot." + declaration->cpp_name;
            const auto value = emit_expression(declaration->initializer);
            out_ << "  store " << llvm_type(declaration->type) << ' '
                 << value.operand << ", ptr " << slot << '\n';
            if (is_string_like(declaration->type)) {
                out_ << "  store i64 " << value.string_length << ", ptr %sun.len."
                     << declaration->cpp_name << '\n';
            }
        } else if (const auto* assignment =
                       std::get_if<AssignmentStatement>(&statement.node)) {
            const auto found = variables_.find(assignment->cpp_name);
            if (found == variables_.end()) unsupported("assignment target");
            const auto value = emit_expression(assignment->value);
            out_ << "  store " << llvm_type(found->second) << ' ' << value.operand
                 << ", ptr %sun.slot." << assignment->cpp_name << '\n';
            if (is_string_like(found->second)) {
                out_ << "  store i64 " << value.string_length << ", ptr %sun.len."
                     << assignment->cpp_name << '\n';
            }
        } else if (const auto* returned = std::get_if<ReturnStatement>(&statement.node)) {
            if (returned->value) {
                const auto value = emit_expression(*returned->value);
                if (is_string_like(value.type)) {
                    out_ << "  store i64 " << value.string_length
                         << ", ptr %sun.return.len\n";
                }
                out_ << "  ret " << llvm_type(value.type) << ' ' << value.operand << '\n';
            } else {
                out_ << "  ret void\n";
            }
            terminated_ = true;
            } else if (const auto* call = std::get_if<CallStatement>(&statement.node)) {
                if (call->callee == "sun_list_append") {
                    const auto list = emit_expression(call->arguments.at(0));
                    const auto item = emit_expression(call->arguments.at(1));
                    const auto suffix = llvm_list_suffix(item.type);
                    if (is_string_like(item.type)) {
                        out_ << "  call void @sun_llvm_list_append_string(ptr "
                             << list.operand << ", ptr " << item.operand << ", i64 "
                             << item.string_length << ")\n";
                    } else {
                        out_ << "  call void @sun_llvm_list_append_" << suffix
                             << "(ptr " << list.operand << ", " << llvm_type(item.type)
                             << " " << item.operand << ")\n";
                    }
                } else if (call->callee == "sun_list_insert") {
                    const auto list = emit_expression(call->arguments.at(0));
                    auto index = emit_expression(call->arguments.at(1));
                    const auto item = emit_expression(call->arguments.at(2));
                    if (index.type == Type::integer) {
                        const auto converted = temporary();
                        out_ << "  " << converted << " = sitofp i64 " << index.operand
                             << " to double\n";
                        index = {Type::number, converted};
                    }
                    const auto suffix = llvm_list_suffix(item.type);
                    if (is_string_like(item.type)) {
                        out_ << "  call void @sun_llvm_list_insert_string(ptr "
                             << list.operand << ", double " << index.operand
                             << ", ptr " << item.operand << ", i64 "
                             << item.string_length << ")\n";
                    } else {
                        out_ << "  call void @sun_llvm_list_insert_" << suffix
                             << "(ptr " << list.operand << ", double " << index.operand
                             << ", " << llvm_type(item.type) << " " << item.operand
                             << ")\n";
                    }
                } else if (call->callee == "sun_list_remove") {
                    const auto list = emit_expression(call->arguments.at(0));
                    const auto item = emit_expression(call->arguments.at(1));
                    const auto suffix = llvm_list_suffix(item.type);
                    if (is_string_like(item.type)) {
                        out_ << "  call i1 @sun_llvm_list_remove_string(ptr "
                             << list.operand << ", ptr " << item.operand << ", i64 "
                             << item.string_length << ")\n";
                    } else {
                        out_ << "  call i1 @sun_llvm_list_remove_" << suffix
                             << "(ptr " << list.operand << ", " << llvm_type(item.type)
                             << " " << item.operand << ")\n";
                    }
                } else if (call->callee == "sun_list_pop") {
                    const auto list = emit_expression(call->arguments.at(0));
                    auto index = emit_expression(call->arguments.at(1));
                    if (index.type == Type::integer) {
                        const auto converted = temporary();
                        out_ << "  " << converted << " = sitofp i64 " << index.operand
                             << " to double\n";
                        index = {Type::number, converted};
                    }
                    const auto item_type = list_item_type(call->arguments.at(0).type);
                    const auto suffix = llvm_list_suffix(item_type);
                    if (is_string_like(item_type)) {
                        const auto length_slot = temporary();
                        out_ << "  " << length_slot << " = alloca i64\n"
                             << "  call ptr @sun_llvm_list_pop_string(ptr "
                             << list.operand << ", double " << index.operand
                             << ", ptr " << length_slot << ")\n";
                    } else {
                        out_ << "  call " << llvm_type(item_type)
                             << " @sun_llvm_list_pop_" << suffix << "(ptr "
                             << list.operand << ", double " << index.operand << ")\n";
                    }
                } else {
                const auto function = functions_.find(call->callee);
                if (function == functions_.end()) unsupported("call to '" + call->callee + "'");
                std::ostringstream arguments;
                emit_call_arguments(arguments, call->arguments, *function->second);
                auto argument_text = arguments.str();
                if (function->second->return_type &&
                    is_string_like(*function->second->return_type)) {
                    const auto length_slot = temporary();
                    out_ << "  " << length_slot << " = alloca i64\n";
                    if (!argument_text.empty()) argument_text += ", ";
                    argument_text += "ptr " + length_slot;
                }
                out_ << "  call "
                 << (function->second->return_type
                     ? llvm_type(*function->second->return_type)
                     : "void")
                 << " @" << call->callee << '(' << argument_text << ")\n";
                }
        } else if (const auto* indexed =
                       std::get_if<IndexedAssignmentStatement>(&statement.node)) {
            if (indexed->kind == IndexedAssignmentStatement::Kind::list &&
                indexed->object.kind == Expression::Kind::call &&
                indexed->object.value == "sun_list_at" &&
                indexed->object.children.size() == 2) {
                const auto list = emit_expression(indexed->object.children[0]);
                auto index = emit_expression(indexed->object.children[1]);
                const auto item = emit_expression(indexed->value);
                if (index.type == Type::integer) {
                    const auto converted = temporary();
                    out_ << "  " << converted << " = sitofp i64 " << index.operand
                         << " to double\n";
                    index = {Type::number, converted};
                }
                const auto suffix = llvm_list_suffix(item.type);
                if (is_string_like(item.type)) {
                    out_ << "  call void @sun_llvm_list_set_string(ptr "
                         << list.operand << ", double " << index.operand
                         << ", ptr " << item.operand << ", i64 "
                         << item.string_length << ")\n";
                } else {
                    out_ << "  call void @sun_llvm_list_set_" << suffix
                         << "(ptr " << list.operand << ", double " << index.operand
                         << ", " << llvm_type(item.type) << " " << item.operand
                         << ")\n";
                }
            } else {
                unsupported("indexed assignment for non-list collection");
            }
        } else if (const auto* member_call =
                       std::get_if<MemberCallStatement>(&statement.node)) {
            if (member_call->method == "clear" &&
                is_list_type(member_call->receiver.type)) {
                const auto list = emit_expression(member_call->receiver);
                const auto suffix = llvm_list_suffix(
                    list_item_type(member_call->receiver.type));
                out_ << "  call void @sun_llvm_list_clear_" << suffix
                     << "(ptr " << list.operand << ")\n";
            } else {
                unsupported("member call '" + member_call->method + "'");
            }
        } else if (const auto* control =
                       std::get_if<LoopControlStatement>(&statement.node)) {
            if (loop_targets_.empty()) unsupported("loop control outside a loop");
            out_ << "  br label %"
                 << (control->kind == LoopControlStatement::Kind::break_
                         ? loop_targets_.back().first
                         : loop_targets_.back().second)
                 << '\n';
            terminated_ = true;
        } else if (const auto* conditional = std::get_if<IfStatement>(&statement.node)) {
            emit_if(*conditional);
        } else if (const auto* loop = std::get_if<WhileStatement>(&statement.node)) {
            emit_while(*loop);
        } else if (const auto* loop =
                       std::get_if<RangeForStatement>(&statement.node)) {
            emit_range_for(*loop);
        } else if (const auto* loop =
                       std::get_if<ForEachStatement>(&statement.node)) {
            emit_for_each(*loop);
        } else {
            unsupported("statement");
        }
    }

    void emit_block(const Block& block) {
        for (const auto& statement : block) {
            if (terminated_) break;
            emit_statement(statement);
        }
    }

    void predeclare_block(const Block& block) {
        for (const auto& statement : block) {
            if (const auto* declaration =
                    std::get_if<VariableDeclarationStatement>(&statement.node)) {
                variables_.emplace(declaration->cpp_name, declaration->type);
            } else if (const auto* conditional =
                           std::get_if<IfStatement>(&statement.node)) {
                for (const auto& branch : conditional->branches) {
                    predeclare_block(branch.body);
                }
                if (conditional->else_body) predeclare_block(*conditional->else_body);
            } else if (const auto* loop =
                           std::get_if<WhileStatement>(&statement.node)) {
                predeclare_block(loop->body);
            } else if (const auto* loop =
                           std::get_if<RangeForStatement>(&statement.node)) {
                variables_.emplace(loop->variable_name, Type::number);
                predeclare_block(loop->body);
            } else if (const auto* loop =
                           std::get_if<ForEachStatement>(&statement.node)) {
                if (is_list_type(loop->collection.type)) {
                    variables_.emplace(loop->variable_name,
                                       list_item_type(loop->collection.type));
                } else {
                    unsupported("for-in collection type '" + cpp_type(loop->collection.type) + "'");
                }
                predeclare_block(loop->body);
            }
        }
    }

    void emit_if(const IfStatement& statement) {
        const auto end_label = label("if.end");
        for (const auto& branch : statement.branches) {
            const auto condition = emit_expression(branch.condition);
            const auto body_label = label("if.body");
            const auto next_label = label("if.next");
            out_ << "  br i1 " << condition.operand << ", label %" << body_label
                 << ", label %" << next_label << '\n'
                 << body_label << ":\n";
            terminated_ = false;
            emit_block(branch.body);
            if (!terminated_) out_ << "  br label %" << end_label << '\n';
            out_ << next_label << ":\n";
            terminated_ = false;
        }
        if (statement.else_body) {
            emit_block(*statement.else_body);
            if (!terminated_) out_ << "  br label %" << end_label << '\n';
        } else {
            out_ << "  br label %" << end_label << '\n';
        }
        out_ << end_label << ":\n";
        terminated_ = false;
    }

    void emit_range_for(const RangeForStatement& statement) {
        const auto start = emit_expression(statement.start_value);
        const auto stop = emit_expression(statement.stop_value);
        const auto step = emit_expression(statement.step_value);
        out_ << "  store double " << start.operand << ", ptr %sun.slot."
             << statement.variable_name << '\n';

        const auto step_zero_fail = label("range.zero.fail");
        const auto step_zero_ok = label("range.zero.ok");
        const auto condition_label = label("range.cond");
        const auto body_label = label("range.body");
        const auto increment_label = label("range.increment");
        const auto end_label = label("range.end");

        const auto step_zero = temporary();
        out_ << "  " << step_zero << " = fcmp oeq double " << step.operand
             << ", 0.0\n"
             << "  br i1 " << step_zero << ", label %" << step_zero_fail
             << ", label %" << step_zero_ok << '\n'
             << step_zero_fail << ":\n"
             << "  call void @llvm.trap()\n"
             << "  unreachable\n"
             << step_zero_ok << ":\n"
             << "  br label %" << condition_label << '\n'
             << condition_label << ":\n";

        const auto variable = temporary();
        out_ << "  " << variable << " = load double, ptr %sun.slot."
             << statement.variable_name << '\n';
        const auto positive = temporary();
        const auto positive_condition = temporary();
        const auto negative_condition = temporary();
        out_ << "  " << positive << " = fcmp ogt double " << step.operand
             << ", 0.0\n"
             << "  " << positive_condition << " = fcmp olt double "
             << variable << ", " << stop.operand << '\n'
             << "  " << negative_condition << " = fcmp ogt double "
             << variable << ", " << stop.operand << '\n';

        const auto selected = temporary();
        out_ << "  " << selected << " = select i1 " << positive
             << ", i1 " << positive_condition << ", i1 " << negative_condition
             << '\n'
             << "  br i1 " << selected << ", label %" << body_label
             << ", label %" << end_label << '\n'
             << body_label << ":\n";

        terminated_ = false;
        loop_targets_.emplace_back(end_label, increment_label);
        emit_block(statement.body);
        loop_targets_.pop_back();

        if (!terminated_) {
            out_ << "  br label %" << increment_label << '\n';
        }
        out_ << increment_label << ":\n";
        const auto current = temporary();
        const auto next = temporary();
        out_ << "  " << current << " = load double, ptr %sun.slot."
             << statement.variable_name << '\n'
             << "  " << next << " = fadd double " << current << ", "
             << step.operand << '\n'
             << "  store double " << next << ", ptr %sun.slot."
             << statement.variable_name << '\n'
             << "  br label %" << condition_label << '\n';
        out_ << end_label << ":\n";
        terminated_ = false;
    }

    void emit_for_each(const ForEachStatement& statement) {
        if (statement.dictionary || !is_list_type(statement.collection.type)) {
            unsupported("for-in over non-list collections");
        }
        const auto collection = emit_expression(statement.collection);
        const auto item_type = list_item_type(statement.collection.type);
        const auto suffix = llvm_list_suffix(item_type);

        const auto index_slot = temporary();
        const auto index_start = temporary();
        const auto index_alloca = "%" + index_slot.substr(1) + ".slot";
        out_ << "  " << index_start << " = alloca i64\n"
             << "  store i64 0, ptr " << index_start << '\n';

        const auto condition_label = label("foreach.cond");
        const auto body_label = label("foreach.body");
        const auto increment_label = label("foreach.increment");
        const auto end_label = label("foreach.end");

        out_ << "  br label %" << condition_label << '\n'
             << condition_label << ":\n";
        const auto index = temporary();
        out_ << "  " << index << " = load i64, ptr " << index_start << '\n';
        const auto length = temporary();
        out_ << "  " << length << " = call i64 @sun_llvm_list_len_" << suffix
             << "(ptr " << collection.operand << ")\n";
        const auto more = temporary();
        out_ << "  " << more << " = icmp slt i64 " << index << ", " << length << '\n'
             << "  br i1 " << more << ", label %" << body_label
             << ", label %" << end_label << '\n'
             << body_label << ":\n";

        const auto index_number = temporary();
        out_ << "  " << index_number << " = sitofp i64 " << index << " to double\n";
        if (is_string_like(item_type)) {
            const auto length_slot = temporary();
            out_ << "  " << length_slot << " = alloca i64\n";
            const auto item = temporary();
            out_ << "  " << item << " = call ptr @sun_llvm_list_at_string(ptr "
                 << collection.operand << ", double " << index_number
                 << ", ptr " << length_slot << ")\n";
            const auto item_length = temporary();
            out_ << "  " << item_length << " = load i64, ptr " << length_slot << '\n'
                 << "  store ptr " << item << ", ptr %sun.slot."
                 << statement.variable_name << '\n'
                 << "  store i64 " << item_length << ", ptr %sun.len."
                 << statement.variable_name << '\n';
        } else {
            const auto item = temporary();
            out_ << "  " << item << " = call " << llvm_type(item_type)
                 << " @sun_llvm_list_at_" << suffix << "(ptr "
                 << collection.operand << ", double " << index_number << ")\n"
                 << "  store " << llvm_type(item_type) << " " << item
                 << ", ptr %sun.slot." << statement.variable_name << '\n';
        }

        terminated_ = false;
        loop_targets_.emplace_back(end_label, increment_label);
        emit_block(statement.body);
        loop_targets_.pop_back();
        if (!terminated_) {
            out_ << "  br label %" << increment_label << '\n';
        }
        out_ << increment_label << ":\n";
        const auto next_index = temporary();
        out_ << "  " << next_index << " = add i64 " << index << ", 1\n"
             << "  store i64 " << next_index << ", ptr " << index_start << '\n'
             << "  br label %" << condition_label << '\n'
             << end_label << ":\n";
        terminated_ = false;
    }

    void emit_while(const WhileStatement& statement) {
        const auto condition_label = label("while.cond");
        const auto body_label = label("while.body");
        const auto end_label = label("while.end");
        out_ << "  br label %" << condition_label << '\n'
             << condition_label << ":\n";
        terminated_ = false;
        const auto condition = emit_expression(statement.condition);
        out_ << "  br i1 " << condition.operand << ", label %" << body_label
             << ", label %" << end_label << '\n'
             << body_label << ":\n";
        terminated_ = false;
        loop_targets_.emplace_back(end_label, condition_label);
        emit_block(statement.body);
        loop_targets_.pop_back();
        if (!terminated_) out_ << "  br label %" << condition_label << '\n';
        out_ << end_label << ":\n";
        terminated_ = false;
    }

    std::ostringstream out_;
    std::vector<std::pair<std::string, std::string>> strings_;
    std::unordered_map<std::string, Type> variables_;
    std::unordered_map<std::string, const Function*> functions_;
    std::vector<std::pair<std::string, std::string>> loop_targets_;
    std::size_t next_temp_ = 0;
    std::size_t next_label_ = 0;
    bool terminated_ = false;
};

} // namespace

std::string transpile_llvm(const Program& program) {
    return Generator{}.generate(program);
}

} // namespace sun
