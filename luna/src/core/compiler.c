// compiler.c

#include "stdio.h"
#include "string_util.h"
#include "mem.h"

#include "opcode.h"
#include "object.h"
#include "chunk.h"

#include "compiler.h"

#include <environment.h>

void write_short(Chunk* chunk, uint16_t value) {
    write_chunk(chunk, (value >> 8) & 0xff, 0);
    write_chunk(chunk, value & 0xff, 0);
}

void init_compiler(Compiler* compiler) {
    compiler->chunk = (Chunk*)mmalloc(sizeof(Chunk));
    init_chunk(compiler->chunk);
}

void free_compiler(Compiler* compiler) {
    free_chunk(compiler->chunk);
    mfree(compiler->chunk);
    // compiler->current_function = NULL;
}

void compile_statement(Node* node, Compiler* compiler);

void compile_expression(Node* node, Compiler* compiler) {
    if (node == NULL) {
        write_chunk(compiler->chunk, OP_NIL, 0);
        return;
    }
    switch (node->type) {
        case NODE_NUMBER: {
            double value = node->number.value;
            int constant = add_constant(compiler->chunk, NUMBER_VAL(value));
            write_chunk(compiler->chunk, OP_CONSTANT, node->line);
            write_chunk(compiler->chunk, constant, node->line);
            break;
        }
        case NODE_STRING: {
            ObjString* string = copy_string(node->string.value, slen(node->string.value));
            int constant = add_constant(compiler->chunk, OBJ_VAL(string));
            write_chunk(compiler->chunk, OP_CONSTANT, 0);
            write_chunk(compiler->chunk, (uint8_t)constant, 0);
            break;
        }
        case NODE_LIST: {
            size_t length = node->list.length;
            for (size_t i = 0; i < length; i++) {
                compile_expression(node->list.content[i], compiler);
            }
            write_chunk(compiler->chunk, OP_BUILD_LIST, node->line);
            write_chunk(compiler->chunk, (uint8_t)length, node->line);
            break;
        }
        case NODE_IDENTIFIER: {
            ObjString* name = copy_string(node->identifier.name, slen(node->identifier.name));
            int name_constant = add_constant(compiler->chunk, OBJ_VAL(name));
            write_chunk(compiler->chunk, OP_GET_VARIABLE, 0);
            write_chunk(compiler->chunk, (uint8_t)name_constant, 0);
            break;
        }
        case NODE_BINARY_OP: {
            compile_expression(node->binary_op.left, compiler);
            compile_expression(node->binary_op.right, compiler);

            if (scmp(node->binary_op.op, "+") == 0) {
                write_chunk(compiler->chunk, OP_ADD, node->line);
            } else if (scmp(node->binary_op.op, "-") == 0) {
                write_chunk(compiler->chunk, OP_SUBTRACT, node->line);
            } else if (scmp(node->binary_op.op, "*") == 0) {
                write_chunk(compiler->chunk, OP_MULTIPLY, node->line);
            } else if (scmp(node->binary_op.op, "/") == 0) {
                write_chunk(compiler->chunk, OP_DIVIDE, node->line);
            }
            else if (scmp(node->binary_op.op, "==") == 0) {
                write_chunk(compiler->chunk, OP_EQUAL, 0);
            } else if (scmp(node->binary_op.op, "!=") == 0) {
                write_chunk(compiler->chunk, OP_NOT_EQUAL, 0);
            } else if (scmp(node->binary_op.op, ">=") == 0) {
                write_chunk(compiler->chunk, OP_GREATER_EQUAL, 0);
            } else if (scmp(node->binary_op.op, "<=") == 0) {
                write_chunk(compiler->chunk, OP_LESS_EQUAL, 0);
            } else if (scmp(node->binary_op.op, "<") == 0) {
                write_chunk(compiler->chunk, OP_LESS, 0);
            } else if (scmp(node->binary_op.op, ">") == 0) {
                write_chunk(compiler->chunk, OP_GREATER, 0);
            }
            else {
                pprintf("Unknown binary operator '%s'\n", node->binary_op.op);
            }
            break;
        }
        case NODE_UNARY_OP: {
            compile_expression(node->unary_op.operand, compiler);
            if (scmp(node->unary_op.op, "-") == 0) {
                write_chunk(compiler->chunk, OP_NEGATE, 0);
            }
            else {
                pprintf("Unknown unary operator '%s'\n", node->unary_op.op);
            }
            break;
        }
        case NODE_BLOCK: {
            for (size_t i = 0; i < node->block.count; i++) {
                compile_statement(node->block.statements[i], compiler);
            }
            break;
        }
        case NODE_GETITEM: {
            compile_expression(node->getitem.sequence, compiler);

            bool is_slice = node->getitem.step != NULL;
            if (is_slice) {
                compile_expression(node->getitem.start, compiler);
                compile_expression(node->getitem.end, compiler);
                compile_expression(node->getitem.step, compiler);
                write_chunk(compiler->chunk, OP_SLICE, node->line);
            } else {
                compile_expression(node->getitem.start, compiler);
                write_chunk(compiler->chunk, OP_SUBSCRIPT, node->line);
            }
            break;
        }
        case NODE_FUNCTION_CALL: {
            ObjString* name = copy_string(node->function_call.function_name, slen(node->function_call.function_name));
            int name_constant = add_constant(compiler->chunk, OBJ_VAL(name));
            write_chunk(compiler->chunk, OP_GET_VARIABLE, 0);
            write_chunk(compiler->chunk, (uint8_t)name_constant, 0);

            size_t arg_count = node->function_call.arg_count;
            for (size_t i = 0; i < arg_count; ++i) {
                compile_expression(node->function_call.arguments[i], compiler);
            }

            write_chunk(compiler->chunk, OP_CALL, 0);
            write_chunk(compiler->chunk, (uint8_t)arg_count, 0);
            break;
        }
        default:
            pprintf("Cannot compile expression of type <%d> in line <%d>\n", node->type, node->line);
            break;
    }
}

void compile_statement(Node* node, Compiler* compiler) {
    switch (node->type) {
        case NODE_EXPRESSION_STATEMENT: {
            compile_expression(node->expression_statement.expression, compiler);
            write_chunk(compiler->chunk, OP_POP, 0);
            break;
        }
        case NODE_ASSIGNMENT: {
            compile_expression(node->assignment.right, compiler);
            if (node->assignment.left->type != NODE_IDENTIFIER) {
                pprintf("Invalid assignment target.\n");
                // TODO exit
            }
            ObjString* name = copy_string(node->assignment.left->identifier.name, slen(node->assignment.left->identifier.name));
            int name_constant = add_constant(compiler->chunk, OBJ_VAL(name));

            write_chunk(compiler->chunk, OP_SET_VARIABLE, 0);
            write_chunk(compiler->chunk, (uint8_t)name_constant, 0);
            break;
        }
        case NODE_IF: {
            compile_expression(node->if_statement.condition, compiler);
            int else_jump = write_jump(compiler->chunk, OP_JUMP_IF_FALSE, 0);

            compile_statement(node->if_statement.then_branch, compiler);

            int end_jump = write_jump(compiler->chunk, OP_JUMP, 0);

            patch_jump(compiler->chunk, else_jump);

            if (node->if_statement.else_branch) {
                compile_statement(node->if_statement.else_branch, compiler);
            }

            patch_jump(compiler->chunk, end_jump);
            break;
        }
        case NODE_WHILE: {
            int loop_start = compiler->chunk->count;

            compile_expression(node->while_statement.condition, compiler);

            int exit_jump = write_jump(compiler->chunk, OP_JUMP_IF_FALSE, 0);

            compile_statement(node->while_statement.then_branch, compiler);

            int offset = compiler->chunk->count - loop_start + 3; // OP_LOOP 1, READ_SHORT 2
            write_chunk(compiler->chunk, OP_LOOP, 0);
            write_short(compiler->chunk, offset);

            patch_jump(compiler->chunk, exit_jump);
            break;
        }
        case NODE_FOR: {
            write_chunk(compiler->chunk, OP_NIL, 0);

            Node* element = node->for_statement.element;
            ObjString* name = copy_string(element->identifier.name, slen(node->identifier.name));
            int name_constant = add_constant(compiler->chunk, OBJ_VAL(name));

            write_chunk(compiler->chunk, OP_SET_VARIABLE, 0);
            write_chunk(compiler->chunk, (uint8_t)name_constant, 0);

            compile_expression(node->for_statement.iterable, compiler);
            write_chunk(compiler->chunk, OP_GET_ITERATOR, node->line);

            int loop_start = compiler->chunk->count;

            write_chunk(compiler->chunk, OP_ITERATE, node->line);

            write_chunk(compiler->chunk, OP_SET_VARIABLE, 0);
            write_chunk(compiler->chunk, name_constant, 0);

            int exit_jump = write_jump(compiler->chunk, OP_JUMP_IF_FALSE, 0);

            compile_statement(node->for_statement.then_branch, compiler);

            int offset = compiler->chunk->count - loop_start + 3;
            write_chunk(compiler->chunk, OP_LOOP, 0);
            write_short(compiler->chunk, offset);

            patch_jump(compiler->chunk, exit_jump);
            break;
        }
        case NODE_FUNCTION_DEFINITION: {
            const char* function_name = node->function_definition.function_name;
            ObjString* name = copy_string(function_name, slen(function_name));

            ObjFunction* function = new_function();
            function->arity = (int)node->function_definition.arg_count;
            function->name = name;
            function->arg_names = (char**)mmalloc(sizeof(char*) * function->arity);
            for (int i = 0; i < function->arity; i++) {
                const char* arg_name = node->function_definition.arguments[i]->identifier.name;
                function->arg_names[i] = sdup(arg_name);
            }

            init_chunk(&function->chunk);

            Compiler function_compiler;
            init_compiler(&function_compiler);
            function_compiler.chunk = &function->chunk;
            // function_compiler.current_function = function;

            compile(node->function_definition.content, &function_compiler);

            // free_compiler(&function_compiler);

            int func_constant = add_constant(compiler->chunk, OBJ_VAL(function));
            write_chunk(compiler->chunk, OP_CONSTANT, node->line);
            write_chunk(compiler->chunk, (uint8_t)func_constant, node->line);

            int name_constant = add_constant(compiler->chunk, OBJ_VAL(name));
            write_chunk(compiler->chunk, OP_DEFINE_VARIABLE, node->line);
            write_chunk(compiler->chunk, (uint8_t)name_constant, node->line);

            break;
        }
        case NODE_RETURN: {

            // if (compiler->current_function == NULL) {
            //     pprintf("Cannot return from top-level code.\n");
            //     break;
            // }

            Node* return_value_node = node->return_statement.value;

            if (return_value_node != NULL) {
                compile_expression(return_value_node, compiler);
            } else {
                write_chunk(compiler->chunk, OP_RETURN, node->line);
            }

            write_chunk(compiler->chunk, OP_RETURN, node->line);

            break;
        }
        case NODE_BLOCK: {
            for (size_t i = 0; i < node->block.count; i++) {
                compile_statement(node->block.statements[i], compiler);
            }
            break;
        }
        default:
            pprintf("Cannot compile statement of type <%d> in line <%d>\n", node->type, node->line);
            break;
    }
}

void compile(Node* node, Compiler* compiler) {
    if (node == NULL) return;

    switch (node->type) {
        case NODE_PROGRAM: {
            for (size_t i = 0; i < node->program.count; i++) {
                compile_statement(node->program.statements[i], compiler);
            }
            write_chunk(compiler->chunk, OP_RETURN, 0);
            break;
        }
        case NODE_BLOCK: {
            for (size_t i = 0; i < node->program.count; i++) {
                compile_statement(node->program.statements[i], compiler);
            }
            break;
        }
        default:
            pprintf("Cannot compile node of type %d\n", node->type);
            break;
    }
}
