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

std::string llvm_type(Type type) {
    if (type == Type::integer) return "i64";
    if (type == Type::number) return "double";
    if (type == Type::boolean) return "i1";
    if (type == Type::string) return "ptr";
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
        if (!program.functions.empty()) {
            unsupported("functions");
        }

        predeclare_block(program.main_body);
        for (const auto& [name, type] : variables_) {
            out_ << "  %sun.slot." << name << " = alloca " << llvm_type(type) << '\n';
            if (type == Type::string) {
                out_ << "  %sun.len." << name << " = alloca i64\n";
            }
        }
        if (!variables_.empty()) out_ << '\n';
        emit_block(program.main_body);
        if (!terminated_) out_ << "  ret i32 0\n";

        std::ostringstream module;
        for (const auto& [name, value] : strings_) {
            module << name << " = private unnamed_addr constant ["
                   << value.size() + 1 << " x i8] c\""
                   << llvm_string(value) << "\\00\"\n";
        }
        module << "@.sun.fmt.int = private unnamed_addr constant [6 x i8] c\"%lld\\0A\\00\"\n"
                  "@.sun.fmt.number = private unnamed_addr constant [4 x i8] c\"%g\\0A\\00\"\n"
                  "@.sun.bool.true = private unnamed_addr constant [5 x i8] c\"true\\00\"\n"
                  "@.sun.bool.false = private unnamed_addr constant [6 x i8] c\"false\\00\"\n"
                  "@stdout = external global ptr\n\n"
                  "declare i32 @printf(ptr, ...)\n"
                  "declare i32 @puts(ptr)\n"
                  "declare i64 @fwrite(ptr, i64, i64, ptr)\n"
                  "declare i32 @putchar(i32)\n"
                  "declare void @llvm.trap()\n"
                  "declare { i64, i1 } @llvm.sadd.with.overflow.i64(i64, i64)\n"
                  "declare { i64, i1 } @llvm.ssub.with.overflow.i64(i64, i64)\n"
                  "declare { i64, i1 } @llvm.smul.with.overflow.i64(i64, i64)\n\n"
               << checked_integer_helpers();
        module << "\ndefine i32 @main() {\nentry:\n"
               << out_.str();
        module << "}\n";
        return module.str();
    }

private:
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
            unsupported("literal type '" + cpp_type(expression.type) + "'");
        }
        if (expression.kind == Expression::Kind::string_literal) {
            return {Type::string, string_pointer(expression.value),
                    std::to_string(expression.value.size())};
        }
        if (expression.kind == Expression::Kind::variable) {
            const auto found = variables_.find(expression.value);
            if (found == variables_.end()) {
                unsupported("variable '" + expression.value + "'");
            }
            const auto result = temporary();
            out_ << "  " << result << " = load " << llvm_type(found->second)
                 << ", ptr %sun.slot." << expression.value << '\n';
            if (found->second == Type::string) {
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
            unsupported("cast to '" + expression.value + "'");
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
            if (expression.value == "sun_to_int" &&
                expression.children.size() == 1 &&
                expression.children[0].kind == Expression::Kind::literal &&
                expression.children[0].type == Type::number) {
                const auto number = std::stod(expression.children[0].value);
                if (!std::isfinite(number) ||
                    number < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
                    number >= -static_cast<double>(std::numeric_limits<std::int64_t>::min())) {
                    unsupported("out-of-range int conversion");
                }
                return {Type::integer, std::to_string(static_cast<std::int64_t>(number))};
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
                out_ << "  " << result << " = "
                     << (expression.value == "&&" ? "and" : "or")
                     << " i1 " << left.operand << ", " << right.operand << '\n';
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
            } else {
                unsupported("string comparison");
            }
            return {Type::boolean, result};
        }
        unsupported("binary expression result type '" + cpp_type(expression.type) + "'");
    }

    void emit_print(const PrintStatement& print) {
        const auto value = emit_expression(print.value);
        if (value.type == Type::string) {
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

    void emit_statement(const Statement& statement) {
        if (const auto* print = std::get_if<PrintStatement>(&statement.node)) {
            emit_print(*print);
        } else if (const auto* declaration =
                       std::get_if<VariableDeclarationStatement>(&statement.node)) {
            const auto slot = "%sun.slot." + declaration->cpp_name;
            const auto value = emit_expression(declaration->initializer);
            out_ << "  store " << llvm_type(declaration->type) << ' '
                 << value.operand << ", ptr " << slot << '\n';
            if (declaration->type == Type::string) {
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
            if (found->second == Type::string) {
                out_ << "  store i64 " << value.string_length << ", ptr %sun.len."
                     << assignment->cpp_name << '\n';
            }
        } else if (const auto* returned = std::get_if<ReturnStatement>(&statement.node)) {
            if (returned->value) {
                const auto value = emit_expression(*returned->value);
                out_ << "  ret " << llvm_type(value.type) << ' ' << value.operand << '\n';
            } else {
                out_ << "  ret void\n";
            }
            terminated_ = true;
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
                const auto inserted =
                    variables_.emplace(declaration->cpp_name, declaration->type);
                if (!inserted.second) {
                    unsupported("shadowed local variables in LLVM output");
                }
            } else if (const auto* conditional =
                           std::get_if<IfStatement>(&statement.node)) {
                for (const auto& branch : conditional->branches) {
                    predeclare_block(branch.body);
                }
                if (conditional->else_body) predeclare_block(*conditional->else_body);
            } else if (const auto* loop =
                           std::get_if<WhileStatement>(&statement.node)) {
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
