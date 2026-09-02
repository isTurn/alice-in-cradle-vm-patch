using dnlib.DotNet;
using dnlib.DotNet.Emit;

class Program
{
    static int Main(string[] args)
    {
        string src = args.Length > 0 ? args[0] : @"D:\Download\Alice in Cradle Trial Version\AliceInCradle_Data\Managed\unsafeAssem.dll";
        string dst = args.Length > 1 ? args[1] : src;

        var asm = AssemblyDef.Load(src);
        var mod = asm.ManifestModule;

        if (args.Length > 2 && args[2] == "dump")
        {
            var inType2 = mod.Find("XX.IN", false);
            if (inType2 == null) { Console.WriteLine("ERROR: XX.IN not found"); return 1; }
            var awake2 = inType2.FindMethod("Awake");
            var body2 = awake2.Body;
            foreach (var instr in body2.Instructions)
            {
                string operand = "";
                if (instr.Operand != null)
                {
                    operand = instr.Operand.ToString();
                    if (instr.Operand is Instruction target)
                        operand = "-> IL_" + target.Offset.ToString("X4");
                    else if (instr.Operand is string s)
                        operand = "\"" + s + "\"";
                    else if (instr.Operand is int iv)
                        operand = iv.ToString();
                    else if (instr.Operand is MethodDef md)
                        operand = md.FullName;
                    else if (instr.Operand is FieldDef fd)
                        operand = fd.FullName;
                }
                Console.WriteLine("IL_{0:X4}: {1,-12} {2}", instr.Offset, instr.OpCode.Name, operand);
            }
            return 0;
        }

        var inType = mod.Find("XX.IN", false);
        if (inType == null) { Console.WriteLine("ERROR: XX.IN not found"); return 1; }
        var awake = inType.FindMethod("Awake");
        if (awake == null) { Console.WriteLine("ERROR: Awake not found"); return 1; }
        var body = awake.Body;
        var instrs = body.Instructions;

        // Sanity check: the VM/environment detection must contain "virgl" string.
        bool hasVirgl = false;
        foreach (var instr in instrs)
            if (instr.OpCode == OpCodes.Ldstr && instr.Operand is string s && s == "virgl")
                hasVirgl = true;
        if (!hasVirgl)
        {
            Console.WriteLine("ERROR: 'virgl' detection string not found in XX.IN::Awake. Game version may have changed its detection logic.");
            return 1;
        }

        // Locate the environment-check's `flag = false`:
        //   ldc.i4.0 ; stloc.0 ; ldloc.0   (the ldloc.0 is the `if (!flag)` load)
        // The version-integrity branch also sets flag=false but is followed by `br`, not `ldloc.0`,
        // so this pattern uniquely identifies the environment detection, independent of IL offsets.
        Instruction target1 = null, target2 = null;
        for (int i = 0; i < instrs.Count - 2; i++)
        {
            if (instrs[i].OpCode == OpCodes.Ldc_I4_0
                && instrs[i + 1].OpCode == OpCodes.Stloc_0
                && instrs[i + 2].OpCode == OpCodes.Ldloc_0)
            {
                target1 = instrs[i];
                target2 = instrs[i + 1];
                break;
            }
        }

        if (target1 == null || target2 == null)
        {
            Console.WriteLine("ERROR: could not locate environment-check flag=false pattern (ldc.i4.0;stloc.0;ldloc.0).");
            Console.WriteLine("NOTE: the 'virgl' check still exists but the flag=false pattern is gone.");
            Console.WriteLine("The file is most likely ALREADY PATCHED (no action needed), or its detection logic changed.");
            return 2;
        }
        Console.WriteLine("Found env-check flag=false at IL_{0:X4}/IL_{1:X4}", target1.Offset, target2.Offset);

        target1.OpCode = OpCodes.Nop;
        target1.Operand = null;
        target2.OpCode = OpCodes.Nop;
        target2.Operand = null;

        asm.Write(dst);
        Console.WriteLine("PATCHED OK -> " + dst);
        return 0;
    }
}
