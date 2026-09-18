# Hermes profile: Syntesa gateway (prebuilt model)

The gateway ships with no model selected (`model: ""`). This profile
preselects the Syntesa OpenAI-compatible gateway so the user only adds
one API key.

## Setup (one time per machine)

```sh
mkdir -p ~/.hermes
cp hermes-profile/config.yaml ~/.hermes/config.yaml
cp hermes-profile/.env.example ~/.hermes/.env
# edit ~/.hermes/.env: set OPENAI_API_KEY to your Syntesa key (gwk_...)
```

## What is preselected

- Provider: `openai-api` (reads `OPENAI_API_KEY`, honors `OPENAI_BASE_URL`)
- Endpoint: `https://ai.syntesa.net/v1` (verified live: `/v1/models`
  lists `qwen3.8-27b`; `/v1/chat/completions` round-trips)
- Main model: `qwen3.8-27b`
- Auxiliary slots: untouched (`auto`, follow the main model)

## Verify

```sh
hermes config get model --json
hermes chat  # new session picks up qwen3.8-27b
```

New sessions read `config.yaml`; running chats keep their old model
(use `/model` inside chat to hot-swap).
