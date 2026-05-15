#include "internal.h"
void build_text_char_meta(const char* text, int char_count, TextCharMeta* metas,
                                 int* out_word_count, int* out_line_count) {
    int word_count = 0;
    int line_count = 0;
    int current_line = 0;
    int current_word = -1;
    bool in_word = false;

    if (!text || !metas || char_count <= 0) {
        if (out_word_count) *out_word_count = 0;
        if (out_line_count) *out_line_count = 0;
        return;
    }

    line_count = 1;
    for (int i = 0; i < char_count; i++) {
        char c = text[i];
        bool is_newline = (c == '\n' || c == '\r');
        bool is_space = !is_newline && (c == ' ' || c == '\t' || c == '\f' || c == '\v');
        metas[i].line_index = current_line;
        metas[i].word_index = current_word;
        metas[i].word_selectable = false;
        metas[i].line_selectable = !is_newline;

        if (is_newline) {
            current_line += 1;
            line_count = current_line + 1;
            current_word = -1;
            in_word = false;
            continue;
        }

        if (is_space) {
            current_word = -1;
            in_word = false;
            continue;
        }

        if (!in_word) {
            current_word = word_count++;
            in_word = true;
        }
        metas[i].word_index = current_word;
        metas[i].word_selectable = true;
    }

    if (out_word_count) *out_word_count = word_count;
    if (out_line_count) *out_line_count = line_count;
}

bool resolve_selector_unit(TextSelectorBasedOn based_on, const TextCharMeta* meta,
                                  int char_index, int char_count, int total_words, int total_lines,
                                  int* out_index, int* out_count) {
    if (!out_index || !out_count) return false;
    switch (based_on) {
        case TEXT_SELECTOR_BASED_ON_WORDS:
            if (!meta || !meta->word_selectable || meta->word_index < 0 || total_words <= 0) return false;
            *out_index = meta->word_index;
            *out_count = total_words;
            return true;
        case TEXT_SELECTOR_BASED_ON_LINES:
            if (!meta || !meta->line_selectable || meta->line_index < 0 || total_lines <= 0) return false;
            *out_index = meta->line_index;
            *out_count = total_lines;
            return true;
        case TEXT_SELECTOR_BASED_ON_CHARACTERS:
        default:
            if (char_count <= 0) return false;
            *out_index = char_index;
            *out_count = char_count;
            return true;
    }
}

static const char* skip_expr_ws(const char* s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    return s;
}

static bool expr_match(TextExpressionParser* parser, const char* token) {
    size_t len;
    parser->cursor = skip_expr_ws(parser->cursor);
    len = strlen(token);
    if (strncmp(parser->cursor, token, len) == 0) {
        parser->cursor += len;
        return true;
    }
    return false;
}

static double parse_text_expr_or(TextExpressionParser* parser);

static double evaluate_text_expr_function(const char* name, double* args, int argc, bool* ok) {
    *ok = true;
    if (strcmp(name, "sin") == 0 && argc == 1) return sin(args[0]);
    if (strcmp(name, "cos") == 0 && argc == 1) return cos(args[0]);
    if (strcmp(name, "tan") == 0 && argc == 1) return tan(args[0]);
    if (strcmp(name, "abs") == 0 && argc == 1) return fabs(args[0]);
    if (strcmp(name, "floor") == 0 && argc == 1) return floor(args[0]);
    if (strcmp(name, "ceil") == 0 && argc == 1) return ceil(args[0]);
    if (strcmp(name, "round") == 0 && argc == 1) return round(args[0]);
    if (strcmp(name, "sqrt") == 0 && argc == 1) return sqrt(fmax(args[0], 0.0));
    if (strcmp(name, "exp") == 0 && argc == 1) return exp(args[0]);
    if (strcmp(name, "log") == 0 && argc == 1) return log(fmax(args[0], 1e-9));
    if (strcmp(name, "min") == 0 && argc == 2) return fmin(args[0], args[1]);
    if (strcmp(name, "max") == 0 && argc == 2) return fmax(args[0], args[1]);
    if (strcmp(name, "clamp") == 0 && argc == 3) return fmin(fmax(args[0], args[1]), args[2]);
    if (strcmp(name, "mix") == 0 && argc == 3) return args[0] + (args[1] - args[0]) * args[2];
    if (strcmp(name, "smoothstep") == 0 && argc == 3) {
        double t = (fabs(args[1] - args[0]) < 1e-9) ? 0.0 : (args[2] - args[0]) / (args[1] - args[0]);
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
        return t * t * (3.0 - 2.0 * t);
    }
    if (strcmp(name, "frac") == 0 && argc == 1) return args[0] - floor(args[0]);
    if (strcmp(name, "sign") == 0 && argc == 1) return (args[0] > 0.0) - (args[0] < 0.0);
    if (strcmp(name, "pow") == 0 && argc == 2) return pow(args[0], args[1]);
    if (strcmp(name, "mod") == 0 && argc == 2) return fmod(args[0], args[1]);
    if (strcmp(name, "step") == 0 && argc == 2) return args[1] < args[0] ? 0.0 : 1.0;
    if (strcmp(name, "ifelse") == 0 && argc == 3) return fabs(args[0]) > 1e-9 ? args[1] : args[2];
    *ok = false;
    return 0.0;
}

static bool lookup_text_expr_identifier(const TextExpressionContext* ctx, const char* name, double* out_value) {
    if (strcmp(name, "i") == 0 || strcmp(name, "index") == 0) *out_value = ctx->index;
    else if (strcmp(name, "count") == 0 || strcmp(name, "n") == 0) *out_value = ctx->count;
    else if (strcmp(name, "t") == 0 || strcmp(name, "time") == 0) *out_value = ctx->time;
    else if (strcmp(name, "p") == 0 || strcmp(name, "position") == 0) *out_value = ctx->position;
    else if (strcmp(name, "charIndex") == 0) *out_value = ctx->char_index;
    else if (strcmp(name, "charCount") == 0 || strcmp(name, "chars") == 0) *out_value = ctx->char_count;
    else if (strcmp(name, "wordIndex") == 0) *out_value = ctx->word_index;
    else if (strcmp(name, "wordCount") == 0 || strcmp(name, "words") == 0) *out_value = ctx->word_count;
    else if (strcmp(name, "lineIndex") == 0) *out_value = ctx->line_index;
    else if (strcmp(name, "lineCount") == 0 || strcmp(name, "lines") == 0) *out_value = ctx->line_count;
    else if (strcmp(name, "pi") == 0 || strcmp(name, "PI") == 0) *out_value = 3.141592653589793;
    else if (strcmp(name, "tau") == 0 || strcmp(name, "TAU") == 0) *out_value = 6.283185307179586;
    else return false;
    return true;
}

static double parse_text_expr_primary(TextExpressionParser* parser) {
    char ident[64];
    double args[8];
    char* endptr = NULL;
    int argc = 0;
    bool ok = false;
    double value = 0.0;

    parser->cursor = skip_expr_ws(parser->cursor);
    if (expr_match(parser, "(")) {
        value = parse_text_expr_or(parser);
        if (!expr_match(parser, ")")) parser->error = true;
        return value;
    }
    if ((*parser->cursor >= '0' && *parser->cursor <= '9') || *parser->cursor == '.') {
        value = strtod(parser->cursor, &endptr);
        if (endptr == parser->cursor) {
            parser->error = true;
            return 0.0;
        }
        parser->cursor = endptr;
        return value;
    }
    if (((*parser->cursor >= 'A' && *parser->cursor <= 'Z') ||
         (*parser->cursor >= 'a' && *parser->cursor <= 'z') ||
         *parser->cursor == '_')) {
        int len = 0;
        while (((parser->cursor[len] >= 'A' && parser->cursor[len] <= 'Z') ||
                (parser->cursor[len] >= 'a' && parser->cursor[len] <= 'z') ||
                (parser->cursor[len] >= '0' && parser->cursor[len] <= '9') ||
                parser->cursor[len] == '_') && len < (int)sizeof(ident) - 1) {
            ident[len] = parser->cursor[len];
            len++;
        }
        ident[len] = '\0';
        parser->cursor += len;
        parser->cursor = skip_expr_ws(parser->cursor);
        if (expr_match(parser, "(")) {
            parser->cursor = skip_expr_ws(parser->cursor);
            if (!expr_match(parser, ")")) {
                do {
                    if (argc >= 8) {
                        parser->error = true;
                        return 0.0;
                    }
                    args[argc++] = parse_text_expr_or(parser);
                    parser->cursor = skip_expr_ws(parser->cursor);
                } while (expr_match(parser, ","));
                if (!expr_match(parser, ")")) {
                    parser->error = true;
                    return 0.0;
                }
            }
            value = evaluate_text_expr_function(ident, args, argc, &ok);
            if (!ok) parser->error = true;
            return value;
        }
        if (!lookup_text_expr_identifier(parser->ctx, ident, &value)) {
            parser->error = true;
            return 0.0;
        }
        return value;
    }
    parser->error = true;
    return 0.0;
}

static double parse_text_expr_unary(TextExpressionParser* parser) {
    if (expr_match(parser, "+")) return parse_text_expr_unary(parser);
    if (expr_match(parser, "-")) return -parse_text_expr_unary(parser);
    if (expr_match(parser, "!")) return fabs(parse_text_expr_unary(parser)) > 1e-9 ? 0.0 : 1.0;
    return parse_text_expr_primary(parser);
}

static double parse_text_expr_power(TextExpressionParser* parser) {
    double value = parse_text_expr_unary(parser);
    while (expr_match(parser, "^")) {
        value = pow(value, parse_text_expr_unary(parser));
    }
    return value;
}

static double parse_text_expr_term(TextExpressionParser* parser) {
    double value = parse_text_expr_power(parser);
    for (;;) {
        if (expr_match(parser, "*")) value *= parse_text_expr_power(parser);
        else if (expr_match(parser, "/")) value /= parse_text_expr_power(parser);
        else if (expr_match(parser, "%")) value = fmod(value, parse_text_expr_power(parser));
        else break;
    }
    return value;
}

static double parse_text_expr_add(TextExpressionParser* parser) {
    double value = parse_text_expr_term(parser);
    for (;;) {
        if (expr_match(parser, "+")) value += parse_text_expr_term(parser);
        else if (expr_match(parser, "-")) value -= parse_text_expr_term(parser);
        else break;
    }
    return value;
}

static double parse_text_expr_compare(TextExpressionParser* parser) {
    double value = parse_text_expr_add(parser);
    for (;;) {
        if (expr_match(parser, "<=")) value = value <= parse_text_expr_add(parser) ? 1.0 : 0.0;
        else if (expr_match(parser, ">=")) value = value >= parse_text_expr_add(parser) ? 1.0 : 0.0;
        else if (expr_match(parser, "<")) value = value < parse_text_expr_add(parser) ? 1.0 : 0.0;
        else if (expr_match(parser, ">")) value = value > parse_text_expr_add(parser) ? 1.0 : 0.0;
        else break;
    }
    return value;
}

static double parse_text_expr_equal(TextExpressionParser* parser) {
    double value = parse_text_expr_compare(parser);
    for (;;) {
        if (expr_match(parser, "==")) value = fabs(value - parse_text_expr_compare(parser)) < 1e-9 ? 1.0 : 0.0;
        else if (expr_match(parser, "!=")) value = fabs(value - parse_text_expr_compare(parser)) >= 1e-9 ? 1.0 : 0.0;
        else break;
    }
    return value;
}

static double parse_text_expr_and(TextExpressionParser* parser) {
    double value = parse_text_expr_equal(parser);
    while (expr_match(parser, "&&")) {
        value = (fabs(value) > 1e-9 && fabs(parse_text_expr_equal(parser)) > 1e-9) ? 1.0 : 0.0;
    }
    return value;
}

static double parse_text_expr_or(TextExpressionParser* parser) {
    double value = parse_text_expr_and(parser);
    while (expr_match(parser, "||")) {
        value = (fabs(value) > 1e-9 || fabs(parse_text_expr_and(parser)) > 1e-9) ? 1.0 : 0.0;
    }
    return value;
}

double evaluate_text_expression(const char* expression, const TextExpressionContext* ctx, bool* out_ok) {
    TextExpressionParser parser;
    double value;
    if (out_ok) *out_ok = false;
    if (!expression || !ctx) return 0.0;
    parser.cursor = expression;
    parser.ctx = ctx;
    parser.error = false;
    value = parse_text_expr_or(&parser);
    parser.cursor = skip_expr_ws(parser.cursor);
    if (parser.error || *parser.cursor != '\0') return 0.0;
    if (out_ok) *out_ok = true;
    return value;
}
