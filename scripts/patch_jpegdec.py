"""
PlatformIO pre-build script: patch JPEGDEC for MCU_SKIP progressive JPEG crashes.

The fork already carries the pMCU redirection fix in some libdep checkouts,
while upstream now carries the same idea as patch files plus DC-write guards.
Apply the source transform idempotently so either checkout shape builds cleanly.
"""

Import("env")  # noqa: F821
import os


def patch_jpegdec(env):
    libdeps_dir = os.path.join(env["PROJECT_DIR"], ".pio", "libdeps")
    if not os.path.isdir(libdeps_dir):
        return
    for env_dir in os.listdir(libdeps_dir):
        jpeg_inl = os.path.join(libdeps_dir, env_dir, "JPEGDEC", "src", "jpeg.inl")
        if os.path.isfile(jpeg_inl):
            _apply_mcu_skip_fixes(jpeg_inl)


def _apply_mcu_skip_fixes(filepath):
    with open(filepath, "r", encoding="utf-8") as f:
        content = f.read()

    original = content

    old_pointer = "    signed short *pMCU = &pJPEG->sMCUs[iMCU & 0xffffff];"
    new_pointer = (
        "    // CrossPoint patch: safe pMCU for MCU_SKIP\n"
        "    signed short *pMCU = (iMCU < 0) ? pJPEG->sMCUs\n"
        "                                     : &pJPEG->sMCUs[iMCU & 0xffffff];"
    )
    if old_pointer in content:
        content = content.replace(old_pointer, new_pointer, 1)

    old_sa_write = "                pMCU[0] |= iPositive;"
    new_sa_write = (
        "                if (iMCU >= 0)\n"
        "                    pMCU[0] |= iPositive;"
    )
    if old_sa_write in content and new_sa_write not in content:
        content = content.replace(old_sa_write, new_sa_write, 1)

    old_dc_write = "        pMCU[0] = (short)*iDCPredictor; // store in MCU[0]"
    new_dc_write = (
        "        if (iMCU >= 0)\n"
        "            pMCU[0] = (short)*iDCPredictor; // store in MCU[0]"
    )
    if old_dc_write in content and new_dc_write not in content:
        content = content.replace(old_dc_write, new_dc_write, 1)

    # Upstream jpegdec_patches/0003: a progressive scan can carry luma without one or
    # both chroma components, and grayscale decode skipped both unconditionally —
    # consuming entropy bits that belong to the next block. The Cb skip also ran with
    # Cr's Huffman tables. Skip only components present in this scan, with their own tables.
    old_chroma_skip = (
        "                    if (pJPEG->ucMode == 0xc2) { // progressive\n"
        "                        iErr |= JPEGDecodeMCU_P(pJPEG, MCU_SKIP, &iDCPred1);\n"
        "                        iErr |= JPEGDecodeMCU_P(pJPEG, MCU_SKIP, &iDCPred2);\n"
    )
    new_chroma_skip = (
        "                    if (pJPEG->ucMode == 0xc2) { // progressive\n"
        "                        // CrossPoint patch: skip only chroma components present in this scan\n"
        "                        if (pJPEG->JPCI[1].component_needed)\n"
        "                            iErr |= JPEGDecodeMCU_P(pJPEG, MCU_SKIP, &iDCPred1);\n"
        "                        if (pJPEG->JPCI[2].component_needed) {\n"
        "                            pJPEG->ucACTable = cACTable2;\n"
        "                            pJPEG->ucDCTable = cDCTable2;\n"
        "                            iErr |= JPEGDecodeMCU_P(pJPEG, MCU_SKIP, &iDCPred2);\n"
        "                        }\n"
    )
    if old_chroma_skip in content:
        content = content.replace(old_chroma_skip, new_chroma_skip, 1)

    if content != original:
        with open(filepath, "w", encoding="utf-8") as f:
            f.write(content)
        print("Patched JPEGDEC MCU_SKIP progressive decode guards: %s" % filepath)


def patch_nimble(env):
    libdeps_dir = os.path.join(env["PROJECT_DIR"], ".pio", "libdeps")
    if not os.path.isdir(libdeps_dir):
        return
    for env_dir in os.listdir(libdeps_dir):
        nimble_json = os.path.join(libdeps_dir, env_dir, "NimBLE-Arduino", "library.json")
        if os.path.isfile(nimble_json):
            bak_path = nimble_json + ".bak"
            if os.path.exists(bak_path):
                try:
                    os.remove(nimble_json)
                except Exception:
                    pass
            else:
                try:
                    os.rename(nimble_json, bak_path)
                except Exception:
                    pass


patch_jpegdec(env)
patch_nimble(env)

