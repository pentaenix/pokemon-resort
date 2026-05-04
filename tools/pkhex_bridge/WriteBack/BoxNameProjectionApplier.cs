using System.Reflection;
using System.Text.Json;
using PKHeX.Core;

namespace PKHeXBridge.WriteBack;

/// <summary>
/// Applies <c>box_names</c> from JSON via reflection on <see cref="SaveFile"/> (PKHeX surface varies by game).
/// Failures are never silent: missing API or PKHeX rejection surfaces as <see cref="InvalidOperationException"/>.
/// </summary>
public static class BoxNameProjectionApplier
{
    private const BindingFlags MethodFlags =
        BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;

    public static void Apply(SaveFile sav, JsonElement boxNames)
    {
        var setBoxName = FindSetBoxNameStringMethod(sav.GetType());

        var i = 0;
        foreach (var el in boxNames.EnumerateArray())
        {
            if (i >= sav.BoxCount)
            {
                break;
            }

            var name = el.ValueKind == JsonValueKind.String ? el.GetString() : null;
            if (!string.IsNullOrWhiteSpace(name))
            {
                try
                {
                    if (setBoxName is not null)
                    {
                        setBoxName.Invoke(sav, new object[] { i, name! });
                    }
                    else
                    {
                        // Fallback when reflection can't see string overload (e.g. only ReadOnlySpan<char> binding).
                        SetBoxNameViaDynamic(sav, i, name!);
                    }
                }
                catch (TargetInvocationException tie) when (tie.InnerException is not null)
                {
                    throw new InvalidOperationException(
                        $"set_box_name_failed box_index={i} detail={tie.InnerException.Message}",
                        tie.InnerException);
                }
                catch (Microsoft.CSharp.RuntimeBinder.RuntimeBinderException ex)
                {
                    throw new InvalidOperationException(
                        $"set_box_name_unsupported save_type={sav.GetType().FullName}", ex);
                }
                catch (Exception ex)
                {
                    throw new InvalidOperationException($"set_box_name_failed box_index={i} detail={ex.Message}", ex);
                }
            }

            i++;
        }
    }

    private static void SetBoxNameViaDynamic(SaveFile sav, int boxIndex, string name)
    {
        dynamic d = sav;
        d.SetBoxName(boxIndex, name);
    }

    /// <summary>PKHeX exposes <c>SetBoxName(int box, string name)</c> on concrete save types.</summary>
    private static MethodInfo? FindSetBoxNameStringMethod(Type saveRuntimeType)
    {
        foreach (var candidate in saveRuntimeType.GetMethods(MethodFlags))
        {
            if (candidate.Name != "SetBoxName")
            {
                continue;
            }

            var ps = candidate.GetParameters();
            if (ps.Length != 2 || ps[0].ParameterType != typeof(int))
            {
                continue;
            }

            if (ps[1].ParameterType == typeof(string))
            {
                return candidate;
            }
        }

        return null;
    }
}
