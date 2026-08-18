# MakeLLM

Describe a Geometry Dash level in plain words — get a **physics-validated, playable level** built right in the editor. Works with **any OpenAI-compatible LLM endpoint**: Omniroute, Ollama, LM Studio, llama.cpp, OpenAI, OpenRouter, DeepSeek, Groq...

## How it works

1. Open the level editor → press the **LLM** button
2. Describe the level (any language) → **Generate**
3. The built-in physics bot **plays the level before you see it**: unpassable jumps, tight corridors and **secret ways** are sent back to the AI for automatic fixes
4. The result lands on its own editor layer: **Accept**, or **Deny** — your level is always yours

## Setup

Open mod settings (Geode → Mods → MakeLLM → ⚙):

- **Base URL** — e.g. `http://localhost:20128/v1` (Omniroute default)
- **API Key** — leave empty for local servers
- **Model** — the model name your endpoint expects

## Highlights

- OpenAI-compatible: one Base URL + key + model, and you're done
- Physics validator: cube jump arcs, flight-corridor gaps, orb/pad chains, speed portals
- Anti-secret-way: rejects zero-input completions and ceiling-less flight sections
- Refinement rounds: the AI fixes its own mistakes before staging
- Unique seed: random theme/palette/mechanic per generation
- Safe staging: preview layer + Accept/Deny + single-step Undo
