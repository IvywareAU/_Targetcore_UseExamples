// Copyright © 2026 Khrustal & Mann
//              MELBOURNE, VICTORIA, AUSTRALIA, 3000
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied. See the License for the specific language governing
// permissions and limitations under the License.
package mscs.p2pf;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.SymbolLookup;
import java.lang.invoke.MethodHandle;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;

/**
 * The whole of the binding layer's machinery: load the DLL, call a C++ vtable
 * slot, build a vtable of our own, and move UTF-16 strings across.
 *
 * <p>There is no generated code here and no native code of ours anywhere. The
 * facade's own header says its interfaces are "pure-vtable ... usable from any
 * MSVC toolset (and any language that can call a vtable)", and this class is
 * that claim taken literally: {@code TargetFacade.dll} is the only input to the
 * build. Java is handed no header, no import library, no type library and no
 * jextract output.
 *
 * <h2>What a call actually is</h2>
 * An interface pointer points at an object whose first 8 bytes are a pointer to
 * its vtable; the vtable is an array of function pointers in <em>declaration
 * order</em>. So calling {@code hub->Listen(a, b)} is: read the vptr, read slot
 * 0, and make a downcall to it passing {@code this} as the first argument. That
 * is what {@link #slot} and the {@code MH_*} handles in {@link Hub} do.
 *
 * <p><b>The slot numbers are the load-bearing part.</b> Nothing checks them: a
 * wrong index lands the call on a different member with different arguments
 * rather than failing to compile — the same hazard {@code dotNetExamples}
 * records for its hand-written COM interop. {@link Abi} therefore keeps every
 * index in one place, beside the signature it belongs to, and {@link
 * Network#selfCheck} calls two of them early for their known answers.
 *
 * <h2>Calling convention</h2>
 * The facade declares {@code __stdcall} on its one free function. On x64 there
 * is a single native convention, so the annotation carries no information and
 * Panama's default linkage is correct for both the free function and every
 * vtable slot. This binding is x64-only for that reason among others.
 */
public final class Native {

    private Native() { }

    public static final Linker LINKER = Linker.nativeLinker();

    /** Size of a pointer. This binding is x64-only. */
    static final long PTR = 8;

    private static SymbolLookup lookup;

    /**
     * Load {@code TargetFacade.dll} and keep it loaded for the life of the JVM.
     *
     * <p>{@code Arena.global()} rather than a closeable one on purpose: the hub
     * pump threads the kernel spawns outlive any scope we could put around this,
     * and unloading the DLL underneath a running pump is not a recoverable
     * event. The process exiting is the only unload.
     */
    public static synchronized void load(Path dll) {
        if (lookup != null) return;
        lookup = SymbolLookup.libraryLookup(dll, Arena.global());
    }

    static MemorySegment symbol(String name) {
        if (lookup == null) throw new IllegalStateException("Native.load() first");
        return lookup.find(name).orElseThrow(
                () -> new UnsatisfiedLinkError("TargetFacade.dll exports no " + name));
    }

    // ── vtable ───────────────────────────────────────────────────────────────

    /**
     * The function pointer in vtable slot {@code index} of the object {@code
     * obj} points at.
     *
     * <p>Both reinterprets are unavoidable: a pointer that came back from native
     * code arrives as a zero-length segment, because the JVM has no way to know
     * how much is behind it. We do know — 8 bytes for the vptr, and 8 per slot —
     * so this is the narrowest widening that lets the read happen.
     */
    public static MemorySegment slot(MemorySegment obj, int index) {
        MemorySegment vptr = obj.reinterpret(PTR).get(java.lang.foreign.ValueLayout.ADDRESS, 0);
        return vptr.reinterpret((index + 1L) * PTR)
                   .getAtIndex(java.lang.foreign.ValueLayout.ADDRESS, index);
    }

    /** A downcall handle whose first parameter is the target address. */
    public static MethodHandle vfn(FunctionDescriptor fd) {
        return LINKER.downcallHandle(fd);
    }

    /** An upcall stub for one slot of a vtable we implement. */
    public static MemorySegment stub(MethodHandle target, FunctionDescriptor fd, Arena arena) {
        return LINKER.upcallStub(target, fd, arena);
    }

    /**
     * Build an object that a C++ caller will see as an instance of a pure-vtable
     * interface: one allocation holding the vtable, and one holding a pointer to
     * it. The returned segment is the interface pointer.
     *
     * <p>The two allocations are separate because the object a C++ compiler
     * builds is separate too — {@code this} points at the vptr, not at the
     * table. Handing back a pointer to the table itself would make every call
     * dereference a function pointer as if it were the vptr.
     */
    public static MemorySegment vtableObject(Arena arena, MemorySegment... slots) {
        MemorySegment vtbl = arena.allocate(PTR * slots.length, PTR);
        for (int i = 0; i < slots.length; i++) {
            vtbl.setAtIndex(java.lang.foreign.ValueLayout.ADDRESS, i, slots[i]);
        }
        MemorySegment obj = arena.allocate(PTR, PTR);
        obj.set(java.lang.foreign.ValueLayout.ADDRESS, 0, vtbl);
        return obj;
    }

    // ── strings ──────────────────────────────────────────────────────────────
    //
    // Every string on this ABI is a NUL-terminated wchar_t*, which on Windows is
    // UTF-16LE -- java.lang.String's own encoding. That is the one piece of luck
    // in this binding: the conversion is a copy, not a transcode, and a Java
    // consumer of MSCS pays nothing for the kernel's Unicode build. (The _u8
    // surface TargetCore_c.h publishes for portability exists because wchar_t is
    // UTF-32 on Linux; on Windows it would be pure overhead here.)

    /** Copy a Java string into the arena as a NUL-terminated wchar_t*. */
    public static MemorySegment wstr(Arena arena, String s) {
        if (s == null) return MemorySegment.NULL;
        return arena.allocateFrom(s, StandardCharsets.UTF_16LE);
    }

    /** Read a NUL-terminated wchar_t* that native code handed back. */
    public static String str(MemorySegment p) {
        if (p == null || p.equals(MemorySegment.NULL)) return null;
        return p.reinterpret(Long.MAX_VALUE).getString(0, StandardCharsets.UTF_16LE);
    }

    /** Copy {@code size} bytes out of a borrowed payload pointer. */
    public static byte[] bytes(MemorySegment p, int size) {
        if (p == null || p.equals(MemorySegment.NULL) || size <= 0) return new byte[0];
        return p.reinterpret(size).toArray(java.lang.foreign.ValueLayout.JAVA_BYTE);
    }

    /**
     * A payload sent with SendText is the UTF-16 string INCLUDING its
     * terminator, so a received one reads back as text directly. Mirrors
     * {@code light::TextOf}.
     */
    public static String textOf(MemorySegment payload, int size) {
        if (payload == null || payload.equals(MemorySegment.NULL) || size < 2) return "<no data>";
        return payload.reinterpret(size).getString(0, StandardCharsets.UTF_16LE);
    }
}
