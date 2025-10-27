module.exports = grammar({
  name: 'jsasta',

  extras: $ => [
    /\s/,
    $.comment,
  ],

  rules: {
    source_file: $ => repeat($._statement),

    _statement: $ => choice(
      $.variable_declaration,
      $.function_declaration,
      $.external_declaration,
      $.struct_declaration,
      $.expression_statement,
      $.return_statement,
      $.if_statement,
      $.for_statement,
      $.while_statement,
      $.block,
    ),

    // Comments
    comment: $ => choice(
      seq('//', /.*/),
      seq('/*', /[^*]*\*+([^/*][^*]*\*+)*/, '/'),
    ),

    // Variable declarations
    variable_declaration: $ => seq(
      choice('var', 'let', 'const'),
      field('name', $.identifier),
      optional(seq(':', field('type', $.type_annotation))),
      optional(seq('=', field('value', $._expression))),
      ';',
    ),

    // Function declarations
    function_declaration: $ => seq(
      'function',
      field('name', $.identifier),
      '(',
      optional($.parameter_list),
      ')',
      optional(seq(':', field('return_type', $.type_annotation))),
      field('body', $.block),
    ),

    // External declarations
    external_declaration: $ => seq(
      'external',
      field('name', $.identifier),
      '(',
      optional($.parameter_list),
      ')',
      optional(seq(':', field('return_type', $.type_annotation))),
      ';',
    ),

    // Struct declarations
    struct_declaration: $ => seq(
      'struct',
      field('name', $.identifier),
      '{',
      repeat($.struct_field),
      '}',
    ),

    struct_field: $ => seq(
      field('name', $.identifier),
      ':',
      field('type', $.type_annotation),
      optional(seq('=', field('default', $._expression))),
      ';',
    ),

    parameter_list: $ => seq(
      $.parameter,
      repeat(seq(',', $.parameter)),
      optional(','),
    ),

    parameter: $ => seq(
      field('name', choice($.identifier, '...')),
      optional(seq(':', field('type', $.type_annotation))),
    ),

    type_annotation: $ => choice(
      'int',
      'string',
      'bool',
      'void',
      $.identifier,
      '...',
    ),

    // Statements
    expression_statement: $ => seq($._expression, ';'),

    return_statement: $ => seq(
      'return',
      optional($._expression),
      ';',
    ),

    if_statement: $ => prec.right(seq(
      'if',
      '(',
      field('condition', $._expression),
      ')',
      field('consequence', $._statement),
      optional(seq('else', field('alternative', $._statement))),
    )),

    for_statement: $ => seq(
      'for',
      '(',
      optional($._expression),
      ';',
      optional($._expression),
      ';',
      optional($._expression),
      ')',
      field('body', $._statement),
    ),

    while_statement: $ => seq(
      'while',
      '(',
      field('condition', $._expression),
      ')',
      field('body', $._statement),
    ),

    block: $ => seq(
      '{',
      repeat($._statement),
      '}',
    ),

    // Expressions
    _expression: $ => choice(
      $.binary_expression,
      $.unary_expression,
      $.update_expression,
      $.assignment_expression,
      $.ternary_expression,
      $.call_expression,
      $.member_expression,
      $.array_expression,
      $.object_expression,
      $.identifier,
      $.number,
      $.string,
      $.true,
      $.false,
      $.parenthesized_expression,
    ),

    binary_expression: $ => choice(
      ...[
        ['||', 1],
        ['&&', 2],
        ['==', 3],
        ['!=', 3],
        ['<', 4],
        ['<=', 4],
        ['>', 4],
        ['>=', 4],
        ['<<', 5],
        ['>>', 5],
        ['+', 6],
        ['-', 6],
        ['*', 7],
        ['/', 7],
        ['%', 7],
        ['&', 8],
      ].map(([operator, precedence]) =>
        prec.left(precedence, seq(
          field('left', $._expression),
          field('operator', operator),
          field('right', $._expression),
        )),
      ),
    ),

    unary_expression: $ => prec(9, seq(
      field('operator', choice('!', '-', '+')),
      field('argument', $._expression),
    )),

    update_expression: $ => choice(
      prec.left(10, seq(
        field('argument', $._expression),
        field('operator', choice('++', '--')),
      )),
      prec.right(10, seq(
        field('operator', choice('++', '--')),
        field('argument', $._expression),
      )),
    ),

    assignment_expression: $ => prec.right(0, seq(
      field('left', choice($.identifier, $.member_expression)),
      field('operator', choice('=', '+=', '-=', '*=', '/=')),
      field('right', $._expression),
    )),

    ternary_expression: $ => prec.right(1, seq(
      field('condition', $._expression),
      '?',
      field('consequence', $._expression),
      ':',
      field('alternative', $._expression),
    )),

    call_expression: $ => prec(11, seq(
      field('function', $._expression),
      '(',
      optional($.argument_list),
      ')',
    )),

    argument_list: $ => seq(
      $._expression,
      repeat(seq(',', $._expression)),
      optional(','),
    ),

    member_expression: $ => prec(11, seq(
      field('object', $._expression),
      choice(
        seq('.', field('property', $.identifier)),
        seq('[', field('property', $._expression), ']'),
      ),
    )),

    array_expression: $ => seq(
      '[',
      optional(seq(
        $._expression,
        repeat(seq(',', $._expression)),
        optional(','),
      )),
      ']',
    ),

    object_expression: $ => seq(
      '{',
      optional(seq(
        $.pair,
        repeat(seq(',', $.pair)),
        optional(','),
      )),
      '}',
    ),

    pair: $ => seq(
      field('key', choice($.identifier, $.string)),
      ':',
      field('value', $._expression),
    ),

    parenthesized_expression: $ => seq('(', $._expression, ')'),

    // Literals
    identifier: $ => /[a-zA-Z_$][a-zA-Z0-9_$]*/,

    number: $ => /\d+(\.\d+)?/,

    string: $ => choice(
      seq('"', repeat(choice(/[^"\\]/, /\\./)), '"'),
      seq("'", repeat(choice(/[^'\\]/, /\\./)), "'"),
    ),

    true: $ => 'true',
    false: $ => 'false',
  },
});
