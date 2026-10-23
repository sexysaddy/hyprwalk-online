# Frozen knife / hands — GoldSrc StudioMDL v10

Only one supplied first-person model is distributed:

- `v_knife.mdl` — exact, unmodified file from `default-knife-frozen.zip`,
  containing the hands, knife, textures and all animation sequences.

There are no firearms or old `pack-transparent` variants in this release.

The knife material (`knifeskin.bmp` embedded in the MDL) is flagged
`STUDIO_NF_ADDITIVE (0x20)` and uses **additive color blending**. The other
three embedded textures (`view_glove.bmp`, `view_finger.bmp`, `view_skin.BMP`)
are opaque. GoldSrc's palette does not encode fractional alpha: the additive
material must be blended with the scene by the renderer; treating it as opaque
makes the dark texture background appear as a black/blue slab.

In `Viewmodel / Hands` choose `CS 1.6 model` and then `Choose / replace model`
if the saved `model_path` points at an older kit. Select this release's
`fork/assets/viewmodels/cs16/v_knife.mdl`. Video settings are independent.

**Redistribution:** these are user-supplied game assets; verify that you have
permission to redistribute them before publishing the ZIP or repository.
