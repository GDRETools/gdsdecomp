using ICSharpCode.Decompiler.CSharp;
using ICSharpCode.Decompiler.CSharp.Syntax;
using ICSharpCode.Decompiler.CSharp.Transforms;
using ICSharpCode.Decompiler.Semantics;
using ICSharpCode.Decompiler.TypeSystem;

namespace GodotMonoDecomp;


/// <summary>
/// This transform is used to remove attributes that are embedded
/// </summary>
public class RemoveEmbeddedAttributes : DepthFirstAstVisitor, IAstTransform
{
	internal static readonly HashSet<string> attributeNames = new HashSet<string>() {
		"System.Runtime.CompilerServices.IsReadOnlyAttribute",
		"System.Runtime.CompilerServices.IsByRefLikeAttribute",
		"System.Runtime.CompilerServices.IsUnmanagedAttribute",
		"System.Runtime.CompilerServices.NullableAttribute",
		"System.Runtime.CompilerServices.NullableContextAttribute",
		"System.Runtime.CompilerServices.NativeIntegerAttribute",
		"System.Runtime.CompilerServices.RefSafetyRulesAttribute",
		"System.Runtime.CompilerServices.ScopedRefAttribute",
		"System.Runtime.CompilerServices.RequiresLocationAttribute",
		"Microsoft.CodeAnalysis.EmbeddedAttribute",
	};

	internal static readonly HashSet<string> nonEmbeddedAttributeNames = new HashSet<string>() {
		// non-embedded attributes, but we still want to remove them
		"System.Runtime.CompilerServices.CompilerFeatureRequiredAttribute",
		"System.Runtime.CompilerServices.RequiredMemberAttribute",
		"System.Runtime.CompilerServices.IsExternalInit",
	};

	public override void VisitAttributeSection(AttributeSection attributeSection)
	{
		foreach (var attribute in attributeSection.Attributes)
		{
			var trr = attribute.Type.Annotation<TypeResolveResult>();
			if (trr == null)
				continue;

			if (attributeNames.Contains(trr.Type.FullName) || nonEmbeddedAttributeNames.Contains(trr.Type.FullName))
			{
				attribute.Remove();
			}
		}

		if (attributeSection.Attributes.Count == 0)
		{
			attributeSection.Remove();
		}
		else
		{
			base.VisitAttributeSection(attributeSection);
		}
	}

	public void Run(AstNode rootNode, TransformContext context)
	{
		rootNode.AcceptVisitor(this);
	}
}
