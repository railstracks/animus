# Invalid examples — must fail validation

Each file here is a regression witness for a security property of the schema.
CI and reviewers run the validation commands in
[docs/api-packages.md §5](../../docs/api-packages.md) expecting **failure**.

## `secret-literal.api-package.json`

A literal token (`"token": "sk-live-..."`) smuggled into the auth section.
Rejected on multiple structural counts: the `bearer` variant requires
`secretRef` and admits no other property (`additionalProperties: false`),
and `actions` cannot be empty. The property being witnessed: **literal
credentials are structurally unrepresentable in a valid manifest** (#21
acceptance, #23).
