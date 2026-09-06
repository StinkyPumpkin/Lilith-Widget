// StripIWant — rewrites Children of Lilith 5.2.0 + its bundled patches without the
// "iWant Widgets.esl" master. The only reference in the whole set is one script
// property (iWidgets on CoL_UI_Widget_Quest); the patches merely inherit the master.
// Usage: StripIWant <inDir> <outDir>
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Binary.Parameters;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Skyrim;

var inDir  = args[0];
var outDir = args[1];
var iwant  = ModKey.FromFileName("iWant Widgets.esl");

int CountRefs(ISkyrimModGetter m) =>
    m.EnumerateFormLinks().Count(l => !l.IsNull && l.FormKey.ModKey == iwant);

void Process(string rel, bool isCol)
{
    var src = Path.Combine(inDir, rel);
    var dst = Path.Combine(outDir, rel);
    Directory.CreateDirectory(Path.GetDirectoryName(dst)!);

    var mod = SkyrimMod.CreateFromBinary(src, SkyrimRelease.SkyrimSE);
    var before = CountRefs(mod);
    Console.WriteLine($"== {rel}: masters=[{string.Join(", ", mod.ModHeader.MasterReferences.Select(x => x.Master.FileName))}] iWant refs={before}");

    if (isCol)
    {
        foreach (var q in mod.Quests)
        {
            var vmad = q.VirtualMachineAdapter;
            if (vmad == null) continue;
            foreach (var s in vmad.Scripts)
            {
                int n = s.Properties.RemoveAll(p => p is ScriptObjectProperty op && op.Object.FormKey.ModKey == iwant);
                if (n > 0) Console.WriteLine($"   {q.EditorID}: removed {n} iWant property(s) from {s.Name}");
            }
            int m = vmad.Scripts.RemoveAll(s => string.Equals(s.Name, "CoL_MCM_Widgets_Page", StringComparison.OrdinalIgnoreCase));
            if (m > 0) Console.WriteLine($"   {q.EditorID}: removed CoL_MCM_Widgets_Page script entry");
        }
    }

    var after = CountRefs(mod);
    if (after != 0) throw new Exception($"{rel}: still {after} iWant references after strip");

    var order = mod.ModHeader.MasterReferences.Select(x => x.Master).Where(x => x != iwant).ToList();
    mod.WriteToBinary(dst, new BinaryWriteParameters
    {
        MastersListContent  = MastersListContentOption.Iterate,
        MastersListOrdering = new MastersListOrderingByLoadOrder(order),
    });

    var check = SkyrimMod.CreateFromBinary(dst, SkyrimRelease.SkyrimSE);
    Console.WriteLine($"   -> masters=[{string.Join(", ", check.ModHeader.MasterReferences.Select(x => x.Master.FileName))}] records={check.EnumerateMajorRecords().Count()} (src {mod.EnumerateMajorRecords().Count()})");
    if (check.ModHeader.MasterReferences.Any(x => x.Master == iwant)) throw new Exception($"{rel}: iWant still a master");
}

Process("ChildrenOfLilith.esp", true);
Process(Path.Combine("Compatibility", "ChildrenOfLilithVancianPatch.esp"), false);
Process(Path.Combine("Handlers", "ASkyrimKiss", "ChildrenOfLilithASKPatch.esp"), false);
Process(Path.Combine("Handlers", "FlowerGirls", "ChildrenOfLilithFGPatch.esp"), false);
Process(Path.Combine("Handlers", "ImmersiveLapSitting", "CoL_ILS_Patch.esp"), false);
Console.WriteLine("DONE");
