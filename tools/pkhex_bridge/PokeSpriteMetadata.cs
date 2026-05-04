using System.Reflection;
using System.Text.Json;
using PKHeX.Core;

namespace PKHeXBridge;

internal static class PokeSpriteMetadata
{
    private sealed record SpeciesEntry(
        string Slug,
        HashSet<string> FormKeys);

    private static readonly Lazy<IReadOnlyDictionary<int, SpeciesEntry>> Species =
        new(LoadSpecies);

    public static string ResolveSpeciesSlug(int species, string fallback)
    {
        if (Species.Value.TryGetValue(species, out var entry) && !string.IsNullOrWhiteSpace(entry.Slug))
            return entry.Slug;
        return fallback;
    }

    public static string ResolveFormKey(PKM pokemon)
    {
        if (!Species.Value.TryGetValue(pokemon.Species, out var entry) || entry.FormKeys.Count == 0)
            return "";

        var formNames = TryGetFormNames(pokemon);
        var formName = pokemon.Form >= 0 && pokemon.Form < formNames.Count
            ? formNames[pokemon.Form]
            : "";
        var formArgumentName = TryGetNamedFormArgument(pokemon);

        foreach (var candidate in BuildFormCandidates(formName, formArgumentName))
        {
            if (entry.FormKeys.Contains(candidate))
                return candidate;
        }

        // Fallback: some generations/species don't round-trip cleanly through FormConverter strings,
        // but the numeric form index is stable. Provide targeted mappings for common multi-form species
        // where the sprite set expects a short key (east/summer/etc).
        // Note: We only do this when metadata declares these keys, to avoid inventing unknown forms.
        var fallback = FallbackFormKeyFromIndex(pokemon.Species, pokemon.Form);
        if (!string.IsNullOrWhiteSpace(fallback) && entry.FormKeys.Contains(fallback))
            return fallback;

        return "";
    }

    private static string FallbackFormKeyFromIndex(int species, int form)
    {
        // Shellos/Gastrodon: form 0 = West Sea (default "$"), form 1 = East Sea ("east")
        if (species is 422 or 423)
            return form == 1 ? "east" : "$";

        // Burmy/Wormadam: 0 = Plant (default "$"), 1 = Sandy, 2 = Trash
        if (species is 412 or 413)
            return form switch { 1 => "sandy", 2 => "trash", _ => "$" };

        // Deerling/Sawsbuck seasons: 0 = Spring (default "$"), 1 = Summer, 2 = Autumn, 3 = Winter
        if (species is 585 or 586)
            return form switch { 1 => "summer", 2 => "autumn", 3 => "winter", _ => "$" };

        return "";
    }

    private static IReadOnlyDictionary<int, SpeciesEntry> LoadSpecies()
    {
        var result = new Dictionary<int, SpeciesEntry>();
        var metadataPath = FindMetadataPath();
        if (metadataPath is null || !File.Exists(metadataPath))
            return result;

        using var document = JsonDocument.Parse(File.ReadAllText(metadataPath));
        if (document.RootElement.ValueKind != JsonValueKind.Object)
            return result;

        foreach (var property in document.RootElement.EnumerateObject())
        {
            if (!int.TryParse(property.Name, out var speciesId) || property.Value.ValueKind != JsonValueKind.Object)
                continue;

            var slug = "";
            if (property.Value.TryGetProperty("slug", out var slugObj) &&
                slugObj.ValueKind == JsonValueKind.Object &&
                slugObj.TryGetProperty("eng", out var slugValue) &&
                slugValue.ValueKind == JsonValueKind.String)
            {
                slug = NormalizeToken(slugValue.GetString() ?? "");
            }

            var formKeys = new HashSet<string>(StringComparer.Ordinal);
            if (TryGetStyleForms(property.Value, out var formsElement))
            {
                foreach (var formProperty in formsElement.EnumerateObject())
                {
                    formKeys.Add(NormalizeToken(formProperty.Name));
                }
            }

            if (!string.IsNullOrWhiteSpace(slug) || formKeys.Count > 0)
                result[speciesId] = new SpeciesEntry(slug, formKeys);
        }

        return result;
    }

    private static bool TryGetStyleForms(JsonElement speciesElement, out JsonElement formsElement)
    {
        if (speciesElement.TryGetProperty("gen-8", out var gen8) &&
            gen8.ValueKind == JsonValueKind.Object &&
            gen8.TryGetProperty("forms", out formsElement) &&
            formsElement.ValueKind == JsonValueKind.Object)
        {
            return true;
        }

        foreach (var property in speciesElement.EnumerateObject())
        {
            if (!property.Name.StartsWith("gen-", StringComparison.Ordinal) || property.Value.ValueKind != JsonValueKind.Object)
                continue;
            if (property.Value.TryGetProperty("forms", out formsElement) &&
                formsElement.ValueKind == JsonValueKind.Object)
            {
                return true;
            }
        }

        formsElement = default;
        return false;
    }

    private static string? FindMetadataPath()
    {
        static IEnumerable<string> CandidateDirectories()
        {
            yield return AppContext.BaseDirectory;
            yield return Directory.GetCurrentDirectory();
        }

        foreach (var start in CandidateDirectories().Distinct(StringComparer.Ordinal))
        {
            var current = new DirectoryInfo(start);
            while (current is not null)
            {
                var direct = Path.Combine(current.FullName, "assets", "pokesprite", "data", "pokemon.json");
                if (File.Exists(direct))
                    return direct;

                var nested = Path.Combine(current.FullName, "pokemon-resort", "assets", "pokesprite", "data", "pokemon.json");
                if (File.Exists(nested))
                    return nested;

                current = current.Parent;
            }
        }

        return null;
    }

    private static IReadOnlyList<string> TryGetFormNames(PKM pokemon)
    {
        try
        {
            var strings = GameInfo.Strings;
            var types = GetStringList(strings, "Types", "types");
            var forms = GetStringList(strings, "Forms", "forms");
            var genders = GetStringList(strings, "Genders", "genders");
            return FormConverter.GetFormList((ushort)pokemon.Species, types, forms, genders, pokemon.Context);
        }
        catch
        {
            return Array.Empty<string>();
        }
    }

    private static IReadOnlyList<string> GetStringList(object target, params string[] propertyNames)
    {
        foreach (var propertyName in propertyNames)
        {
            var property = target.GetType().GetProperty(
                propertyName,
                BindingFlags.Public | BindingFlags.Instance | BindingFlags.IgnoreCase);
            var value = property?.GetValue(target);
            switch (value)
            {
                case IReadOnlyList<string> list:
                    return list;
                case IEnumerable<string> enumerable:
                    return enumerable.ToArray();
            }
        }

        return Array.Empty<string>();
    }

    private static string TryGetNamedFormArgument(PKM pokemon)
    {
        try
        {
            if (!FormConverter.GetFormArgumentIsNamedIndex((ushort)pokemon.Species))
                return "";

            var formArgument = TryGetIntProperty(pokemon, "FormArgument");
            if (!formArgument.HasValue || formArgument.Value < 0)
                return "";

            var argumentNames = FormConverter.GetFormArgumentStrings((ushort)pokemon.Species);
            if (formArgument.Value >= argumentNames.Length)
                return "";

            return argumentNames[formArgument.Value];
        }
        catch
        {
            return "";
        }
    }

    private static IEnumerable<string> BuildFormCandidates(string formName, string formArgumentName)
    {
        var candidates = new List<string>();
        AddCandidateVariants(candidates, formName);

        var argumentCandidates = new List<string>();
        if (!string.IsNullOrWhiteSpace(formArgumentName))
            AddCandidateVariants(argumentCandidates, formArgumentName);

        if (argumentCandidates.Count > 0)
        {
            var baseCandidates = candidates.Count == 0 ? new List<string> { "" } : candidates.ToList();
            foreach (var baseCandidate in baseCandidates)
            {
                foreach (var argumentCandidate in argumentCandidates)
                {
                    if (string.IsNullOrWhiteSpace(baseCandidate))
                        candidates.Add(argumentCandidate);
                    else
                        candidates.Add($"{baseCandidate}-{argumentCandidate}");
                }
            }
        }

        return candidates
            .Where(static candidate => !string.IsNullOrWhiteSpace(candidate))
            .Distinct(StringComparer.Ordinal);
    }

    private static void AddCandidateVariants(List<string> sink, string raw)
    {
        var normalized = NormalizeToken(raw);
        if (string.IsNullOrWhiteSpace(normalized))
            return;

        void Add(string candidate)
        {
            if (!string.IsNullOrWhiteSpace(candidate))
                sink.Add(candidate);
        }

        Add(normalized);

        foreach (var (from, to) in ExactAliases)
        {
            if (normalized == from)
                Add(to);
        }

        foreach (var suffix in SuffixAliases)
        {
            if (normalized.EndsWith(suffix, StringComparison.Ordinal) && normalized.Length > suffix.Length)
                Add(normalized[..^suffix.Length]);
        }
    }

    private static string NormalizeToken(string raw)
    {
        if (string.IsNullOrWhiteSpace(raw))
            return "";

        if (raw == "$")
            return "$";
        if (raw == "!")
            return "exclamation";
        if (raw == "?")
            return "question";

        var chars = new List<char>(raw.Length);
        var previousWasSeparator = false;
        foreach (var ch in raw.Trim().ToLowerInvariant())
        {
            if (char.IsLetterOrDigit(ch))
            {
                chars.Add(ch);
                previousWasSeparator = false;
                continue;
            }

            if (chars.Count == 0 || previousWasSeparator)
                continue;

            chars.Add('-');
            previousWasSeparator = true;
        }

        var normalized = new string(chars.ToArray()).Trim('-');
        return normalized;
    }

    private static int? TryGetIntProperty(object target, string propertyName)
    {
        var property = target.GetType().GetProperty(propertyName, BindingFlags.Public | BindingFlags.Instance);
        if (property?.GetValue(target) is null)
            return null;

        try
        {
            return Convert.ToInt32(property.GetValue(target));
        }
        catch
        {
            return null;
        }
    }

    private static readonly (string from, string to)[] ExactAliases =
    {
        ("gigantamax", "gmax"),
        ("g-max", "gmax"),
        ("alolan", "alola"),
        ("galarian", "galar"),
        ("hisuian", "hisui"),
        ("paldean", "paldea"),
        ("question-mark", "question"),
        ("exclamation-mark", "exclamation"),
    };

    private static readonly string[] SuffixAliases =
    {
        "-forme",
        "-form",
        "-mode",
        "-style",
        "-pattern",
        "-trim",
        "-cloak",
        "-sea",
        "-season",
        "-flower",
        "-plumage",
        "-size",
        "-coat",
    };
}
