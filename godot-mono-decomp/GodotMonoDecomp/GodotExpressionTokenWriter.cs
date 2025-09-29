// Copyright (c) 2010-2013 AlphaSierraPapa for the SharpDevelop Team
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

using System;
using System.Globalization;
using System.IO;
using System.Text;
using ICSharpCode.Decompiler;
using ICSharpCode.Decompiler.CSharp;
using ICSharpCode.Decompiler.CSharp.OutputVisitor;
using ICSharpCode.Decompiler.CSharp.Syntax;
using ICSharpCode.Decompiler.TypeSystem;
using ICSharpCode.Decompiler.Util;

namespace GodotMonoDecomp;


public class GodotExpressionOutputVisitor : CSharpOutputVisitor
{

	TextWriter tw;

	public GodotExpressionOutputVisitor(TextWriter w, CSharpFormattingOptions formattingOptions)
		: base(new GodotExpressionTokenWriter(w), formattingOptions)
	{
		tw = w;
	}



	public GodotExpressionOutputVisitor(TextWriter w)
		: this(w, SingleLineFormattingOptions)
	{}

	public static readonly CSharpFormattingOptions SingleLineFormattingOptions = GetSingleLineFormattingOptions();

	private static CSharpFormattingOptions GetSingleLineFormattingOptions()
	{
		var settings = new DecompilerSettings();
		var csfmt = settings.CSharpFormattingOptions;
		// set literally everything to false
		csfmt.Name = "SingleLine";
		csfmt.IsBuiltIn = false;
		csfmt.IndentationString = "";
		csfmt.IndentNamespaceBody = false;
		csfmt.IndentClassBody = false;
		csfmt.IndentInterfaceBody = false;
		csfmt.IndentStructBody = false;
		csfmt.IndentEnumBody = false;
		csfmt.IndentMethodBody = false;
		csfmt.IndentPropertyBody = false;
		csfmt.IndentEventBody = false;
		csfmt.IndentBlocks = false;
		csfmt.IndentSwitchBody = false;
		csfmt.IndentCaseBody = false;
		csfmt.IndentBreakStatements = false;
		csfmt.AlignEmbeddedStatements = false;
		csfmt.AlignElseInIfStatements = false;
		csfmt.AutoPropertyFormatting = PropertyFormatting.SingleLine;
		csfmt.SimplePropertyFormatting = PropertyFormatting.SingleLine;
		csfmt.EmptyLineFormatting = EmptyLineFormatting.DoNotChange;
		csfmt.IndentPreprocessorDirectives = false;
		csfmt.AlignToMemberReferenceDot = false;
		csfmt.IndentBlocksInsideExpressions = false;
		csfmt.NamespaceBraceStyle = BraceStyle.EndOfLine;
		csfmt.ClassBraceStyle = BraceStyle.EndOfLine;
		csfmt.InterfaceBraceStyle = BraceStyle.EndOfLine;
		csfmt.StructBraceStyle = BraceStyle.EndOfLine;
		csfmt.EnumBraceStyle = BraceStyle.EndOfLine;
		csfmt.MethodBraceStyle = BraceStyle.EndOfLine;
		csfmt.AnonymousMethodBraceStyle = BraceStyle.EndOfLine;
		csfmt.ConstructorBraceStyle = BraceStyle.EndOfLine;
		csfmt.DestructorBraceStyle = BraceStyle.EndOfLine;
		csfmt.PropertyBraceStyle = BraceStyle.EndOfLine;
		csfmt.PropertyGetBraceStyle = BraceStyle.EndOfLine;
		csfmt.PropertySetBraceStyle = BraceStyle.EndOfLine;
		csfmt.SimpleGetBlockFormatting = PropertyFormatting.SingleLine;
		csfmt.SimpleSetBlockFormatting = PropertyFormatting.SingleLine;
		csfmt.EventBraceStyle = BraceStyle.EndOfLine;
		csfmt.EventAddBraceStyle = BraceStyle.EndOfLine;
		csfmt.EventRemoveBraceStyle = BraceStyle.EndOfLine;
		csfmt.AllowEventAddBlockInline = false;
		csfmt.AllowEventRemoveBlockInline = false;
		csfmt.StatementBraceStyle = BraceStyle.EndOfLine;
		csfmt.AllowIfBlockInline = false;
		csfmt.AllowOneLinedArrayInitialziers = true;
		csfmt.ElseNewLinePlacement = NewLinePlacement.SameLine;
		csfmt.ElseIfNewLinePlacement = NewLinePlacement.SameLine;
		csfmt.CatchNewLinePlacement = NewLinePlacement.SameLine;
		csfmt.FinallyNewLinePlacement = NewLinePlacement.SameLine;
		csfmt.WhileNewLinePlacement = NewLinePlacement.SameLine;
		csfmt.EmbeddedStatementPlacement = NewLinePlacement.SameLine;
		csfmt.NewLineBeforeConstructorInitializerColon = NewLinePlacement.SameLine;
		csfmt.NewLineAfterConstructorInitializerColon = NewLinePlacement.SameLine;

		csfmt.SpaceBetweenParameterAttributeSections = false;
		csfmt.SpaceBeforeMethodDeclarationParentheses = false;
		csfmt.SpaceBetweenEmptyMethodDeclarationParentheses = false;
		csfmt.SpaceBeforeMethodDeclarationParameterComma = false;
		csfmt.SpaceAfterMethodDeclarationParameterComma = false;
		csfmt.SpaceWithinMethodDeclarationParentheses = false;
		csfmt.SpaceBeforeMethodCallParentheses = false;
		csfmt.SpaceBetweenEmptyMethodCallParentheses = false;
		csfmt.SpaceBeforeMethodCallParameterComma = false;
		csfmt.SpaceAfterMethodCallParameterComma = false;
		csfmt.SpaceWithinMethodCallParentheses = false;
		csfmt.SpaceBeforeFieldDeclarationComma = false;
		csfmt.SpaceAfterFieldDeclarationComma = false;
		csfmt.SpaceBeforeLocalVariableDeclarationComma = false;
		csfmt.SpaceAfterLocalVariableDeclarationComma = false;
		csfmt.SpaceBeforeConstructorDeclarationParentheses = false;
		csfmt.SpaceBetweenEmptyConstructorDeclarationParentheses = false;
		csfmt.SpaceBeforeConstructorDeclarationParameterComma = false;
		csfmt.SpaceAfterConstructorDeclarationParameterComma = false;
		csfmt.SpaceWithinConstructorDeclarationParentheses = false;
		csfmt.SpaceBeforeIndexerDeclarationBracket = false;
		csfmt.SpaceWithinIndexerDeclarationBracket = false;
		csfmt.SpaceBeforeIndexerDeclarationParameterComma = false;
		csfmt.SpaceAfterIndexerDeclarationParameterComma = false;
		csfmt.SpaceBeforeDelegateDeclarationParentheses = false;
		csfmt.SpaceBetweenEmptyDelegateDeclarationParentheses = false;
		csfmt.SpaceBeforeDelegateDeclarationParameterComma = false;
		csfmt.SpaceAfterDelegateDeclarationParameterComma = false;
		csfmt.SpaceWithinDelegateDeclarationParentheses = false;
		csfmt.SpaceBeforeNewParentheses = false;
		csfmt.SpaceBeforeIfParentheses = false;
		csfmt.SpaceBeforeWhileParentheses = false;
		csfmt.SpaceBeforeForParentheses = false;
		csfmt.SpaceBeforeForeachParentheses = false;
		csfmt.SpaceBeforeCatchParentheses = false;
		csfmt.SpaceBeforeSwitchParentheses = false;
		csfmt.SpaceBeforeLockParentheses = false;
		csfmt.SpaceBeforeUsingParentheses = false;
		// csfmt.SpaceAroundAssignment = false;
		// csfmt.SpaceAroundLogicalOperator = false;
		// csfmt.SpaceAroundEqualityOperator = false;
		// csfmt.SpaceAroundRelationalOperator = false;
		// csfmt.SpaceAroundBitwiseOperator = false;
		// csfmt.SpaceAroundAdditiveOperator = false;
		// csfmt.SpaceAroundMultiplicativeOperator = false;
		// csfmt.SpaceAroundShiftOperator = false;
		// csfmt.SpaceAroundNullCoalescingOperator = false;
		// csfmt.SpaceAfterUnsafeAddressOfOperator = false;
		// csfmt.SpaceAfterUnsafeAsteriskOfOperator = false;
		// csfmt.SpaceAroundUnsafeArrowOperator = false;
		csfmt.SpacesWithinParentheses = false;
		csfmt.SpacesWithinIfParentheses = false;
		csfmt.SpacesWithinWhileParentheses = false;
		csfmt.SpacesWithinForParentheses = false;
		csfmt.SpacesWithinForeachParentheses = false;
		csfmt.SpacesWithinCatchParentheses = false;
		csfmt.SpacesWithinSwitchParentheses = false;
		csfmt.SpacesWithinLockParentheses = false;
		csfmt.SpacesWithinUsingParentheses = false;
		csfmt.SpacesWithinCastParentheses = false;
		csfmt.SpacesWithinSizeOfParentheses = false;
		csfmt.SpaceBeforeSizeOfParentheses = false;
		csfmt.SpacesWithinTypeOfParentheses = false;
		csfmt.SpacesWithinNewParentheses = false;
		csfmt.SpacesBetweenEmptyNewParentheses = false;
		csfmt.SpaceBeforeNewParameterComma = false;
		csfmt.SpaceAfterNewParameterComma = false;
		csfmt.SpaceBeforeTypeOfParentheses = false;
		csfmt.SpacesWithinCheckedExpressionParantheses = false;
		// csfmt.SpaceBeforeConditionalOperatorCondition = false;
		// csfmt.SpaceAfterConditionalOperatorCondition = false;
		// csfmt.SpaceBeforeConditionalOperatorSeparator = false;
		// csfmt.SpaceAfterConditionalOperatorSeparator = false;
		csfmt.SpaceBeforeAnonymousMethodParentheses = false;
		csfmt.SpaceWithinAnonymousMethodParentheses = false;
		csfmt.SpacesWithinBrackets = false;
		csfmt.SpacesBeforeBrackets = false;
		csfmt.SpaceBeforeBracketComma = false;
		csfmt.SpaceAfterBracketComma = false;
		csfmt.SpaceBeforeForSemicolon = false;
		csfmt.SpaceAfterForSemicolon = false;
		csfmt.SpaceAfterTypecast = false;
		csfmt.SpaceBeforeArrayDeclarationBrackets = false;
		csfmt.SpaceInNamedArgumentAfterDoubleColon = false;
		csfmt.SpaceBeforeSemicolon = false;

		csfmt.RemoveEndOfLineWhiteSpace = true;
		csfmt.MinimumBlankLinesBeforeUsings = 0;
		csfmt.MinimumBlankLinesAfterUsings = 0;
		csfmt.MinimumBlankLinesBeforeFirstDeclaration = 0;
		csfmt.MinimumBlankLinesBetweenTypes = 0;
		csfmt.MinimumBlankLinesBetweenFields = 0;
		csfmt.MinimumBlankLinesBetweenEventFields = 0;
		csfmt.MinimumBlankLinesBetweenMembers = 0;
		csfmt.MinimumBlankLinesAroundRegion = 0;
		csfmt.MinimumBlankLinesInsideRegion = 0;
		csfmt.KeepCommentsAtFirstColumn = false;
		csfmt.ArrayInitializerWrapping = Wrapping.DoNotWrap;
		csfmt.ArrayInitializerBraceStyle = BraceStyle.EndOfLineWithoutSpace;
		csfmt.ChainedMethodCallWrapping = Wrapping.DoNotWrap;
		csfmt.MethodCallArgumentWrapping = Wrapping.DoNotWrap;
		csfmt.NewLineAferMethodCallOpenParentheses = NewLinePlacement.SameLine;
		csfmt.MethodCallClosingParenthesesOnNewLine = NewLinePlacement.SameLine;
		csfmt.IndexerArgumentWrapping = Wrapping.DoNotWrap;
		csfmt.NewLineAferIndexerOpenBracket = NewLinePlacement.SameLine;
		csfmt.IndexerClosingBracketOnNewLine = NewLinePlacement.SameLine;
		csfmt.MethodDeclarationParameterWrapping = Wrapping.DoNotWrap;
		csfmt.NewLineAferMethodDeclarationOpenParentheses = NewLinePlacement.SameLine;
		csfmt.MethodDeclarationClosingParenthesesOnNewLine = NewLinePlacement.SameLine;
		csfmt.IndexerDeclarationParameterWrapping = Wrapping.DoNotWrap;
		csfmt.NewLineAferIndexerDeclarationOpenBracket = NewLinePlacement.SameLine;
		csfmt.IndexerDeclarationClosingBracketOnNewLine = NewLinePlacement.SameLine;
		csfmt.AlignToFirstIndexerArgument = false;
		csfmt.AlignToFirstIndexerDeclarationParameter = false;
		csfmt.AlignToFirstMethodCallArgument = false;
		csfmt.AlignToFirstMethodDeclarationParameter = false;
		csfmt.NewLineBeforeNewQueryClause = NewLinePlacement.SameLine;
		csfmt.UsingPlacement = UsingPlacement.TopOfFile;

		return settings.CSharpFormattingOptions;
	}

	public static string GetString(AstNode node)
	{
		var stringWriter = new StringWriter();
		var visitor = new GodotExpressionOutputVisitor(stringWriter);
		node.AcceptVisitor(visitor);
		return stringWriter.ToString();
	}


	public override void VisitSimpleType(SimpleType simpleType)
	{
		StartNode(simpleType);
		// WriteIdentifier(simpleType.Identifier);
		WriteKeyword(simpleType.ToString(), Roles.Type);
		EndNode(simpleType);
	}

	public override void VisitMemberType(MemberType memberType)
	{
		StartNode(memberType);
		memberType.Target.AcceptVisitor(this);
		if (memberType.IsDoubleColon)
		{
			WriteToken(Roles.DoubleColon);
		}
		else
		{
			WriteToken(Roles.Dot);
		}
		WriteIdentifier(memberType.MemberNameToken);
		WriteTypeArguments(memberType.TypeArguments);
		EndNode(memberType);
	}

	public override void VisitCastExpression(CastExpression castExpression)
	{
		StartNode(castExpression);
		// No casting.
		// LPar();
		// Space(policy.SpacesWithinCastParentheses);
		// castExpression.Type.AcceptVisitor(this);
		// Space(policy.SpacesWithinCastParentheses);
		// RPar();
		// Space(policy.SpaceAfterTypecast);
		castExpression.Expression.AcceptVisitor(this);
		EndNode(castExpression);

	}

	public override void VisitInvocationExpression(InvocationExpression invocationExpression)
	{
		if (invocationExpression.GetSymbol().Name == "Color8")
		{
			int[] args = invocationExpression.Arguments.Select(a => {
				if (a is PrimitiveExpression pe && pe.Value is int i)
				{
					return i;
				}
				return 0;
			}).ToArray();
			float[] dargs = args.Select(a => ((float)a) / 255.0f).ToArray();
			string[] stargs = dargs.Select(a => GodotExpressionTokenWriter.PrintPrimitiveValue(a)).ToArray();
			string value = $"Color({string.Join(",", stargs)})";
			StartNode(invocationExpression);
			writer.WriteToken(Roles.Identifier, value);
			EndNode(invocationExpression);
			return;
		}
		base.VisitInvocationExpression(invocationExpression);
	}

	public override void VisitArrayCreateExpression(ArrayCreateExpression arrayCreateExpression)
	{
		StartNode(arrayCreateExpression);
		// WriteKeyword(ArrayCreateExpression.NewKeywordRole);
		var typename = GodotStuff.CSharpTypeToGodotType(arrayCreateExpression.Type.GetSymbol().Name + "[]");
		// arrayCreateExpression.Type.AcceptVisitor(this);
		// if (arrayCreateExpression.Arguments.Count > 0)
		// {
		// 	WriteCommaSeparatedListInBrackets(arrayCreateExpression.Arguments);
		// }
		// foreach (var specifier in arrayCreateExpression.AdditionalArraySpecifiers)
		// {
		// 	specifier.AcceptVisitor(this);
		// }
		if (typename.StartsWith("Packed"))
		{
			// For Packed* types, we use the Godot type name directly.
			// This is because the C# type name is not the same as the Godot type name.
			writer.WriteToken(Roles.Identifier, typename);
			LPar();
		}
		arrayCreateExpression.Initializer.AcceptVisitor(this);
		if (typename.StartsWith("Packed"))
		{
			RPar();
		}
		EndNode(arrayCreateExpression);
	}

	public override void VisitArrayInitializerExpression(ArrayInitializerExpression arrayInitializerExpression)
	{
		StartNode(arrayInitializerExpression);
		// "new List<int> { { 1 } }" and "new List<int> { 1 }" are the same semantically.
		// We also use the same AST for both: we always use two nested ArrayInitializerExpressions
		// for collection initializers, even if the user did not write nested brackets.
		// The output visitor will output nested braces only if they are necessary,
		// or if the braces tokens exist in the AST.
		// bool bracesAreOptional = arrayInitializerExpression.Elements.Count == 1
		//                          && IsObjectOrCollectionInitializer(arrayInitializerExpression.Parent)
		//                          && !CanBeConfusedWithObjectInitializer(arrayInitializerExpression.Elements.Single());
		// if (bracesAreOptional && arrayInitializerExpression.LBraceToken.IsNull)
		// {
		// 	arrayInitializerExpression.Elements.Single().AcceptVisitor(this);
		// }
		// else
		{
			PrintArrayInitializerElements(arrayInitializerExpression.Elements);
		}
		EndNode(arrayInitializerExpression);
	}

	protected void PrintArrayInitializerElements(AstNodeCollection<Expression> elements)
	{
		bool wrapAlways = policy.ArrayInitializerWrapping == Wrapping.WrapAlways
		                  || (elements.Count > 1 && elements.Any(e => !IsSimpleExpression(e)))
		                  || elements.Any(IsComplexExpression);
		bool wrap = wrapAlways
		            || elements.Count > 10;
		// OpenBrace(wrap ? policy.ArrayInitializerBraceStyle : BraceStyle.EndOfLine, newLine: wrap);
		WriteToken(Roles.LBracket);
		if (!wrap)
			Space();
		AstNode last = null;
		foreach (var (idx, node) in elements.WithIndex())
		{
			if (idx > 0)
			{
				Comma(node, noSpaceAfterComma: true);
				if (wrapAlways || idx % 10 == 0)
					NewLine();
				else
					Space();
			}
			last = node;
			node.AcceptVisitor(this);
		}
		if (last != null)
			OptionalComma(last.NextSibling);
		if (wrap)
			NewLine();
		else
			Space();
		// CloseBrace(wrap ? policy.ArrayInitializerBraceStyle : BraceStyle.EndOfLine, unindent: wrap);
		WriteToken(Roles.RBracket);

		bool IsSimpleExpression(Expression ex)
		{
			switch (ex)
			{
				case NullReferenceExpression _:
				case ThisReferenceExpression _:
				case PrimitiveExpression _:
				case IdentifierExpression _:
				case MemberReferenceExpression {
					Target: ThisReferenceExpression
					or IdentifierExpression
					or BaseReferenceExpression
				} _:
					return true;
				default:
					return false;
			}
		}

		bool IsComplexExpression(Expression ex)
		{
			switch (ex)
			{
				case AnonymousMethodExpression _:
				case LambdaExpression _:
				case AnonymousTypeCreateExpression _:
				case ObjectCreateExpression _:
				case NamedExpression _:
					return true;
				default:
					return false;
			}
		}
	}
	public override void VisitMemberReferenceExpression(MemberReferenceExpression memberReferenceExpression)
	{
		string? text = GodotStuff.ReplaceMemberReference(memberReferenceExpression);
		if (text != null)
		{
			StartNode(memberReferenceExpression);
			writer.WriteToken(Roles.Identifier, text);
			EndNode(memberReferenceExpression);
			return;
		}

		base.VisitMemberReferenceExpression(memberReferenceExpression);
	}


	// public override void VisitPrimitiveExpression(PrimitiveExpression primitiveExpression)
	// {
	// 	if (primitiveExpression.Value is string str)
	// 	{
	// 		Output.WriteString(str);
	// 	}
	// 	else
	// 	{
	// 		base.WritePrimitiveExpression(primitiveExpression);
	// 	}
	// }
}

/// <summary>
/// Writes C# code into a TextWriter.
/// </summary>
public class GodotExpressionTokenWriter : TokenWriter, ILocatable
{
	readonly TextWriter textWriter;
	bool needsIndent = true;
	bool isAtStartOfLine = true;
	int line, column;

	public int Indentation { get; set; }

	public TextLocation Location
	{
		get { return new TextLocation(line, column + (needsIndent ? Indentation * IndentationString.Length : 0)); }
	}

	public string IndentationString { get; set; }

	public int Length { get; private set; }

	public GodotExpressionTokenWriter(TextWriter textWriter)
	{
		if (textWriter == null)
			throw new ArgumentNullException(nameof(textWriter));
		this.textWriter = textWriter;
		this.IndentationString = "\t";
		this.line = 1;
		this.column = 1;
	}

	public override void WriteIdentifier(Identifier identifier)
	{
		WriteIndentation();
		// if (identifier.IsVerbatim || CSharpOutputVisitor.IsKeyword(identifier.Name, identifier))
		// {
		// 	textWriter.Write('@');
		// 	column++;
		// 	Length++;
		// }

		string name = EscapeIdentifier(identifier.Name);
		textWriter.Write(name);
		column += name.Length;
		Length += name.Length;
		isAtStartOfLine = false;
	}

	public override void WriteKeyword(Role role, string keyword)
	{
		if (role == Roles.Type || role == Roles.BaseType)
		{
			keyword = GodotStuff.CSharpTypeToGodotType(keyword);
		}

		WriteIndentation();
		column += keyword.Length;
		Length += keyword.Length;
		textWriter.Write(keyword);
		isAtStartOfLine = false;
	}

	public override void WriteToken(Role role, string token)
	{
		if (role == Roles.Type || role == Roles.BaseType)
		{
			token = GodotStuff.CSharpTypeToGodotType(token);
		}
		WriteIndentation();
		column += token.Length;
		Length += token.Length;
		textWriter.Write(token);
		isAtStartOfLine = false;
	}

	public override void Space()
	{
		WriteIndentation();
		column++;
		Length++;
		textWriter.Write(' ');
	}

	protected void WriteIndentation()
	{
		if (needsIndent)
		{
			needsIndent = false;
			for (int i = 0; i < Indentation; i++)
			{
				textWriter.Write(this.IndentationString);
			}

			column += Indentation * IndentationString.Length;
			Length += Indentation * IndentationString.Length;
		}
	}

	public override void NewLine()
	{
		textWriter.WriteLine();
		column = 1;
		line++;
		Length += textWriter.NewLine.Length;
		needsIndent = true;
		isAtStartOfLine = true;
	}

	public override void Indent()
	{
		Indentation++;
	}

	public override void Unindent()
	{
		Indentation--;
	}

	public override void WriteComment(CommentType commentType, string content)
	{
		// WriteIndentation();
		// switch (commentType)
		// {
		// 	case CommentType.SingleLine:
		// 		textWriter.Write("//");
		// 		textWriter.WriteLine(content);
		// 		Length += 2 + content.Length + textWriter.NewLine.Length;
		// 		column = 1;
		// 		line++;
		// 		needsIndent = true;
		// 		isAtStartOfLine = true;
		// 		break;
		// 	case CommentType.MultiLine:
		// 		textWriter.Write("/*");
		// 		textWriter.Write(content);
		// 		textWriter.Write("*/");
		// 		Length += 4 + content.Length;
		// 		column += 2;
		// 		UpdateEndLocation(content, ref line, ref column);
		// 		column += 2;
		// 		isAtStartOfLine = false;
		// 		break;
		// 	case CommentType.Documentation:
		// 		textWriter.Write("///");
		// 		textWriter.WriteLine(content);
		// 		Length += 3 + content.Length + textWriter.NewLine.Length;
		// 		column = 1;
		// 		line++;
		// 		needsIndent = true;
		// 		isAtStartOfLine = true;
		// 		break;
		// 	case CommentType.MultiLineDocumentation:
		// 		textWriter.Write("/**");
		// 		textWriter.Write(content);
		// 		textWriter.Write("*/");
		// 		Length += 5 + content.Length;
		// 		column += 3;
		// 		UpdateEndLocation(content, ref line, ref column);
		// 		column += 2;
		// 		isAtStartOfLine = false;
		// 		break;
		// 	default:
		// 		textWriter.Write(content);
		// 		column += content.Length;
		// 		Length += content.Length;
		// 		break;
		// }
	}

	static void UpdateEndLocation(string content, ref int line, ref int column)
	{
		if (string.IsNullOrEmpty(content))
			return;
		for (int i = 0; i < content.Length; i++)
		{
			char ch = content[i];
			switch (ch)
			{
				case '\r':
					if (i + 1 < content.Length && content[i + 1] == '\n')
						i++;
					goto case '\n';
				case '\n':
					line++;
					column = 0;
					break;
			}

			column++;
		}
	}

	public override void WritePreProcessorDirective(PreProcessorDirectiveType type, string argument)
	{
		// pre-processor directive must start on its own line
		if (!isAtStartOfLine)
			NewLine();
		WriteIndentation();
		textWriter.Write('#');
		string directive = type.ToString().ToLowerInvariant();
		textWriter.Write(directive);
		column += 1 + directive.Length;
		Length += 1 + directive.Length;
		if (!string.IsNullOrEmpty(argument))
		{
			textWriter.Write(' ');
			textWriter.Write(argument);
			column += 1 + argument.Length;
			Length += 1 + argument.Length;
		}

		NewLine();
	}

	public static string PrintPrimitiveValue(object value)
	{
		TextWriter writer = new StringWriter();
		GodotExpressionTokenWriter tokenWriter = new GodotExpressionTokenWriter(writer);
		tokenWriter.WritePrimitiveValue(value);
		return writer.ToString();
	}

	public override void WritePrimitiveValue(object value, LiteralFormat format = LiteralFormat.None)
	{
		if (value == null)
		{
			// usually NullReferenceExpression should be used for this, but we'll handle it anyways
			textWriter.Write("null");
			column += 4;
			Length += 4;
			return;
		}

		if (value is bool)
		{
			if ((bool)value)
			{
				textWriter.Write("true");
				column += 4;
				Length += 4;
			}
			else
			{
				textWriter.Write("false");
				column += 5;
				Length += 5;
			}

			return;
		}

		if (value is string)
		{
			string tmp = ConvertString(value.ToString());
			column += tmp.Length + 2;
			Length += tmp.Length + 2;
			textWriter.Write('"');
			textWriter.Write(tmp);
			textWriter.Write('"');
			if (format == LiteralFormat.Utf8Literal)
			{
				textWriter.Write("u8");
				column += 2;
				Length += 2;
			}
		}
		else if (value is char)
		{
			string tmp = ConvertCharLiteral((char)value);
			column += tmp.Length + 2;
			Length += tmp.Length + 2;
			textWriter.Write('\'');
			textWriter.Write(tmp);
			textWriter.Write('\'');
		}
		else if (value is decimal)
		{
			string str = ((decimal)value).ToString(NumberFormatInfo.InvariantInfo)/* + "m"*/;
			column += str.Length;
			Length += str.Length;
			textWriter.Write(str);
		}
		else if (value is float)
		{
			float f = (float)value;
			if (float.IsInfinity(f) || float.IsNaN(f))
			{
				// Strictly speaking, these aren't PrimitiveExpressions;
				// but we still support writing these to make life easier for code generators.
				// textWriter.Write("float");
				// column += 5;
				// Length += 5;
				// WriteToken(Roles.Dot, ".");
				if (float.IsPositiveInfinity(f))
				{
					textWriter.Write("INF");
					column += "INF".Length;
					Length += "INF".Length;
				}
				else if (float.IsNegativeInfinity(f))
				{
					textWriter.Write("-INF");
					column += "-INF".Length;
					Length += "-INF".Length;
				}
				else
				{
					textWriter.Write("NAN");
					column += 3;
					Length += 3;
				}

				return;
			}

			var str = f.ToString("R", NumberFormatInfo.InvariantInfo)/* + "f"*/;
			if (f == 0 && 1 / f == float.NegativeInfinity && str[0] != '-')
			{
				// negative zero is a special case
				// (again, not a primitive expression, but it's better to handle
				// the special case here than to do it in all code generators)
				str = '-' + str;
			}

			if (str.IndexOf('.') < 0 && str.IndexOf('E') < 0)
			{
				str += ".0";
			}

			column += str.Length;
			Length += str.Length;
			textWriter.Write(str);
		}
		else if (value is double)
		{
			double f = (double)value;
			if (double.IsInfinity(f) || double.IsNaN(f))
			{
				// Strictly speaking, these aren't PrimitiveExpressions;
				// but we still support writing these to make life easier for code generators.
				// textWriter.Write("double");
				// column += 6;
				// Length += 6;
				// WriteToken(Roles.Dot, ".");
				if (double.IsPositiveInfinity(f))
				{
					textWriter.Write("INF");
					column += "INF".Length;
					Length += "INF".Length;
				}
				else if (double.IsNegativeInfinity(f))
				{
					textWriter.Write("-INF");
					column += "-INF".Length;
					Length += "-INF".Length;
				}
				else
				{
					textWriter.Write("NAN");
					column += 3;
					Length += 3;
				}
				return;
			}

			string number = f.ToString("R", NumberFormatInfo.InvariantInfo);
			if (f == 0 && 1 / f == double.NegativeInfinity && number[0] != '-')
			{
				// negative zero is a special case
				// (again, not a primitive expression, but it's better to handle
				// the special case here than to do it in all code generators)
				number = '-' + number;
			}

			if (number.IndexOf('.') < 0 && number.IndexOf('E') < 0)
			{
				number += ".0";
			}

			textWriter.Write(number);
			column += number.Length;
			Length += number.Length;
		}
		else if (value is IFormattable)
		{
			StringBuilder b = new StringBuilder();
			if (format == LiteralFormat.HexadecimalNumber)
			{
				b.Append("0x");
				b.Append(((IFormattable)value).ToString("X", NumberFormatInfo.InvariantInfo));
			}
			else
			{
				b.Append(((IFormattable)value).ToString(null, NumberFormatInfo.InvariantInfo));
			}

			// if (value is uint || value is ulong)
			// {
			// 	b.Append("u");
			// }
			//
			// if (value is long || value is ulong)
			// {
			// 	b.Append("L");
			// }

			textWriter.Write(b.ToString());
			column += b.Length;
			Length += b.Length;
		}
		else
		{
			textWriter.Write(value.ToString());
			int length = value.ToString().Length;
			column += length;
			Length += length;
		}
	}

	public override void WriteInterpolatedText(string text)
	{
		textWriter.Write(ConvertString(text));
	}

	/// <summary>
	/// Gets the escape sequence for the specified character within a char literal.
	/// Does not include the single quotes surrounding the char literal.
	/// </summary>
	public static string ConvertCharLiteral(char ch)
	{
		if (ch == '\'')
		{
			return "\\'";
		}

		return ConvertChar(ch) ?? ch.ToString();
	}

	/// <summary>
	/// Gets the escape sequence for the specified character.
	/// </summary>
	/// <remarks>This method does not convert ' or ".</remarks>
	static string ConvertChar(char ch)
	{
		switch (ch)
		{
			case '\\':
				return "\\\\";
			case '\0':
				return "\\0";
			case '\a':
				return "\\a";
			case '\b':
				return "\\b";
			case '\f':
				return "\\f";
			case '\n':
				return "\\n";
			case '\r':
				return "\\r";
			case '\t':
				return "\\t";
			case '\v':
				return "\\v";
			case ' ':
			case '_':
			case '`':
			case '^':
				// ASCII characters we allow directly in the output even though we don't use
				// other Unicode characters of the same category.
				return null;
			case '\ufffd':
				return "\\u" + ((int)ch).ToString("x4");
			default:
				switch (char.GetUnicodeCategory(ch))
				{
					case UnicodeCategory.NonSpacingMark:
					case UnicodeCategory.SpacingCombiningMark:
					case UnicodeCategory.EnclosingMark:
					case UnicodeCategory.LineSeparator:
					case UnicodeCategory.ParagraphSeparator:
					case UnicodeCategory.Control:
					case UnicodeCategory.Format:
					case UnicodeCategory.Surrogate:
					case UnicodeCategory.PrivateUse:
					case UnicodeCategory.ConnectorPunctuation:
					case UnicodeCategory.ModifierSymbol:
					case UnicodeCategory.OtherNotAssigned:
					case UnicodeCategory.SpaceSeparator:
						return "\\u" + ((int)ch).ToString("x4");
					default:
						return null;
				}
		}
	}

	/// <summary>
	/// Converts special characters to escape sequences within the given string.
	/// </summary>
	public static string ConvertString(string str)
	{
		StringBuilder sb = new StringBuilder();
		foreach (char ch in str)
		{
			string s = ch == '"' ? "\\\"" : ConvertChar(ch);
			if (s != null)
				sb.Append(s);
			else
				sb.Append(ch);
		}

		return sb.ToString();
	}

	public static string EscapeIdentifier(string identifier)
	{
		if (string.IsNullOrEmpty(identifier))
			return identifier;
		StringBuilder sb = new StringBuilder();
		for (int i = 0; i < identifier.Length; i++)
		{
			if (IsPrintableIdentifierChar(identifier, i))
			{
				if (char.IsSurrogatePair(identifier, i))
				{
					sb.Append(identifier.Substring(i, 2));
					i++;
				}
				else
				{
					sb.Append(identifier[i]);
				}
			}
			else
			{
				if (char.IsSurrogatePair(identifier, i))
				{
					sb.AppendFormat("\\U{0:x8}", char.ConvertToUtf32(identifier, i));
					i++;
				}
				else
				{
					sb.AppendFormat("\\u{0:x4}", (int)identifier[i]);
				}
			}
		}

		return sb.ToString();
	}

	public static bool ContainsNonPrintableIdentifierChar(string identifier)
	{
		if (string.IsNullOrEmpty(identifier))
			return false;

		for (int i = 0; i < identifier.Length; i++)
		{
			if (char.IsWhiteSpace(identifier[i]))
				return true;
			if (!IsPrintableIdentifierChar(identifier, i))
				return true;
		}

		return false;
	}

	static bool IsPrintableIdentifierChar(string identifier, int index)
	{
		switch (identifier[index])
		{
			case '\\':
				return false;
			case ' ':
			case '_':
			case '`':
			case '^':
				return true;
		}

		switch (char.GetUnicodeCategory(identifier, index))
		{
			case UnicodeCategory.NonSpacingMark:
			case UnicodeCategory.SpacingCombiningMark:
			case UnicodeCategory.EnclosingMark:
			case UnicodeCategory.LineSeparator:
			case UnicodeCategory.ParagraphSeparator:
			case UnicodeCategory.Control:
			case UnicodeCategory.Format:
			case UnicodeCategory.Surrogate:
			case UnicodeCategory.PrivateUse:
			case UnicodeCategory.ConnectorPunctuation:
			case UnicodeCategory.ModifierSymbol:
			case UnicodeCategory.OtherNotAssigned:
			case UnicodeCategory.SpaceSeparator:
				return false;
			default:
				return true;
		}
	}

	public override void WritePrimitiveType(string type)
	{
		textWriter.Write(type);
		column += type.Length;
		Length += type.Length;
		if (type == "new")
		{
			textWriter.Write("()");
			column += 2;
			Length += 2;
		}
	}

	public override void StartNode(AstNode node)
	{
		// Write out the indentation, so that overrides of this method
		// can rely use the current output length to identify the position of the node
		// in the output.
		WriteIndentation();
	}

	public override void EndNode(AstNode node)
	{
	}
}
