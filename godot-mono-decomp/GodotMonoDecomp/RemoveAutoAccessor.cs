// Copyright (c) 2011 AlphaSierraPapa for the SharpDevelop Team
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

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

	public override void VisitMethodDeclaration(MethodDeclaration methodDeclaration)
	{
		try
		{
			if (methodDeclaration.GetSymbol() is IMethod method && GodotStuff.IsCompilerGeneratedAccessorMethod(method))
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
