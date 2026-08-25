# Phase 1 — Unload teardown (pass, shader, no resurrect)

Status: not started
Depends on: nothing
Unlocks: phase 2 (runtime CTD). Unload is still not a supported inner loop.
Review issues: **1**, **2**, **5**
Source: [code-review.md](code-review.md)

These three are one `PLUGIN_EXIT` sequence. Doing any one alone still CTDs.

## Why these together

Hyprland holds plugin objects between frames. `PLUGIN_EXIT` today:

1. surgically `removeAllOfType("CShinyPassElement")` on the **top-level** pass
2. `destroyShinyShader()` → `g_shinyShader.reset()` → `~CShader` `glDelete*`
3. `dlclose`

That misses nested `CShinyPassElement`s inside
`CTransformedWindowPassElement::m_data.pass` (motion blur / any transformer).
Those destruct after `dlclose` via an unmapped vtable. If `draw()` of a leftover
element runs after shader destroy, `ensureShinyShader()` compiles a new program
into a dying `.so` (issue 5), then issue 1 still fires.

Issue 2 is the same teardown: `~CShader` always `glDeleteProgram` /
`glDeleteVertexArrays` if `m_program != 0`. `makeEGLCurrent()` is skipped when
`g_pHyprOpenGL` is already null, but `reset()` still runs the deletes with no
context. NVIDIA SIGSEGVs here.

## Files

- `src/main.cpp` — `PLUGIN_EXIT`
- `src/pass.cpp` — `ensureShinyShader`, `destroyShinyShader`
- `src/pass.hpp` — declare the teardown flag if it lives next to the shader
- `src/globals.hpp` — only if the flag is process-global next to `g_cfg`
- `README.md` — the “Do `removeAllOfType`” unload note becomes `m_renderPass.clear()`

## Work

Do this **in this order** inside `PLUGIN_EXIT`, after the existing listener
resets, **before** `dlclose`:

1. **Issue 5 — refuse new shader work.** Set `static bool g_tearingDown` (or
   equivalent) at the **top** of `PLUGIN_EXIT`. `ensureShinyShader()` returns
   false immediately when it is set. This is a guard, not a substitute for
   destroying the pass elements.

2. **Issue 1 — destroy the leftover pass completely.**

   ```cpp
   if (g_pHyprRenderer)
       g_pHyprRenderer->m_renderPass.clear();
   ```

   `clear()` destroys `CTransformedWindowPassElement`, which destroys the nested
   pass, which destroys `CShinyPassElement`, all before `dlclose`.

   Replace `removeAllOfType("CShinyPassElement")`. That call does not recurse.
   `CRenderPass::m_passElements` is private; the plugin cannot walk nested
   passes.

   The leftover pass is already-rendered dead data. Next `beginRender` would
   destroy it anyway. Stock `CBorderPassElement` in that leftover pass is a
   compositor type and was going to die on the next `beginRender` regardless.

   Do **not** `removeAllOfType("CBorderPassElement")` on a *live* pass (that is
   also the stock border). Clearing the leftover pass *after a completed frame*
   is a different operation.

   Do this **before** `destroyShinyShader()`.

3. **Issue 2 — never `glDelete*` without a current EGL context.**

   ```cpp
   void destroyShinyShader() {
       if (!g_pHyprOpenGL)
           return; // process is dying; leak the SP
       g_pHyprOpenGL->makeEGLCurrent();
       g_shinyShader.reset();
   }
   ```

   Only `reset()` when `g_pHyprOpenGL` is alive **and** current. If GL is already
   gone, leak the `SP` — the process is dying. After step 1, no leftover
   `draw()` can resurrect the shader via `ensureShinyShader()` (also refused by
   `g_tearingDown`).

## Out of scope

- Issue 4 (`glBindVertexArray` guard) — same file, different failure mode.
  Phase 2.
- Issue 6 (two `.so` copies) — phase 3. This phase still assumes one load.
- Do not strip decorations yourself; Hyprland does that.

## Done when

- `PLUGIN_EXIT` sets teardown, `m_renderPass.clear()`, then conditionally
  destroys the shader.
- `ensureShinyShader()` cannot compile after teardown starts.
- `destroyShinyShader()` does not `reset()` without `g_pHyprOpenGL`.
- README unload note matches `clear()`, not surgical `removeAllOfType`.

## Verify

In the nest, not the login session:

- Load, draw a window (shader path), unload. Nest stays up.
- Load with a transformer in play (motion blur on, or any `m_transformers`
  entry), unload mid-frame-gap. Nest stays up.
- Unload during compositor teardown (kill the nest while loaded) does not
  NVIDIA-SIGSEGV in `~CShader`.
- `mise run reload` may still fail for issues 3, 4, 6, 7 — that is expected.
