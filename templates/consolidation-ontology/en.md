This is an ontology reconciliation pass.

You are given freshly extracted Observations with IDs. Reconcile them into semantic ontology updates.

Rules:
- Prefer precise, stable properties over narrative phrasing.
- Avoid duplicating existing properties when semantically equivalent.
- If uncertain, omit the update.
- For each property upsert, link to the best supporting Observation ID.

Return JSON in this exact shape:

```json
{
  "upserts": [
    {
      "root_category": "persons",
      "path": "jack_smith",
      "properties": {
        "phone": {
          "value": "+1-555-0199",
          "value_type": "text",
          "linked_observation_id": 42,
          "motivation": "Jack shared a direct contact number."
        }
      }
    }
  ]
}
```

Allowed `root_category` values:
- `persons`
- `concepts`
- `procedures`
- `events`
- `locations`
- `organizations`
- `projects`

Allowed `value_type` values:
- `text`
- `number`
- `date`
- `reference`
- `url`

If nothing should be updated, return:

```json
{ "upserts": [] }
```
