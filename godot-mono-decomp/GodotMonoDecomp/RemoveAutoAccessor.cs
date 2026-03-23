using ICSharpCode.Decompiler.CSharp;
using ICSharpCode.Decompiler.CSharp.Syntax;
using ICSharpCode.Decompiler.CSharp.Transforms;
using ICSharpCode.Decompiler.TypeSystem;

namespace GodotMonoDecomp;

/// <summary>
/// Converts extension method calls into infix syntax.
/// </summary>
public class RemoveAutoAccessor : DepthFirstAstVisitor, IAstTransform
{
	public void Run(AstNode rootNode, TransformContext context)
	{
		rootNode.AcceptVisitor(this);
	}

	public static bool IsCompilerGeneratedAccessorMethod(IMethod method)
	{
		// if it's compiler generated, it won't be marked as virtual and it won't be an actual accessor method
		if (!method.Name.Contains('.') || method.IsVirtual || method.IsAccessor)
		{
			return false;
		}

		bool isAdder = method.Name.Contains(".add_");
		bool isRemover = method.Name.Contains(".remove_");
		bool isInvoker = method.Name.Contains(".invoke_");
		bool isGetter = method.Name.Contains(".get_");
		bool isSetter = method.Name.Contains(".set_");
		if (isGetter || isSetter || isAdder || isRemover || isInvoker)
		{
			var lastDot = method.Name.LastIndexOf('.');
			var parentClass = method.Name.Substring(0, lastDot).Split("<")[0];
			var methodName = method.Name.Substring(lastDot + 1);
			var usidx = methodName.IndexOf('_');
			if (string.IsNullOrEmpty(parentClass) || string.IsNullOrEmpty(methodName) || usidx < 0)
			{
				return false;
			}
			var memberName = methodName.Substring(usidx + 1);
			var baseTypes = method.DeclaringType.GetAllBaseTypes();
			var baseType = baseTypes.FirstOrDefault(t => t.FullName == parentClass);
			if (baseType == null)
			{
				return false;
			}
			IMember? member = baseType.GetMembers().FirstOrDefault(m => m.Name == memberName);

			if ((isGetter || isSetter) && member is IProperty prop)
			{
				var memberAccessorName = isGetter ? prop.Getter?.Name : prop.Setter?.Name;

				if (memberAccessorName == methodName)
				{
					return true;
				}
			} else if (member is IEvent ev)
			{
				var memberAccessorName = isInvoker ? ev.InvokeAccessor?.Name :
					isAdder ? ev.AddAccessor?.Name : isRemover ? ev.RemoveAccessor?.Name : null;
				if (memberAccessorName == methodName)
				{
					return true;
				}
			}
		}
		return false;

	}

	public override void VisitMethodDeclaration(MethodDeclaration methodDeclaration)
	{
		try
		{
			if (methodDeclaration.GetSymbol() is IMethod method && IsCompilerGeneratedAccessorMethod(method))
			{
				methodDeclaration.Remove();
				return;
			}

			base.VisitMethodDeclaration(methodDeclaration);
		}
		finally
		{
		}
	}
}
