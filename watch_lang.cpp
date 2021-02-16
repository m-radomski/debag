static char *
LexerTokenKindToString(token_kind Kind)
{
	switch(Kind)
	{
		case TokenKind_Symbol:
		return "TokenKind_Symbol";
	    case TokenKind_BracketOp:
	    return "TokenKind_BracketOp";
	    case TokenKind_BracketCl:
	    return "TokenKind_BracketCl";
    	case TokenKind_ParenOp:
    	return "TokenKind_ParenOp";
    	case TokenKind_ParenCl:
    	return "TokenKind_ParenCl";
    	case TokenKind_ImmInt:
    	return "TokenKind_ImmInt";
    	case TokenKind_Arrow:
    	return "TokenKind_Arrow";
    	case TokenKind_Dot:
    	return "TokenKind_Dot";
    	case TokenKind_Star:
    	return "TokenKind_Star";
    	case TokenKind_DotDot:
    	return "TokenKind_DotDot";
    	case TokenKind_EOF:
    	return "TokenKind_EOF";
    	default:
    	return "Unknown token kind";
	}
}

static lexer
LexerCreate(char *Content)
{
	lexer Result = {};

	Result.Arena = ArenaCreateZeros(Kilobytes(4));
	Result.Content = StringDuplicate(&Result.Arena, Content);

	return Result;
}

static void
LexerDestroy(lexer *Lexer)
{
	ArenaDestroy(&Lexer->Arena);
}

static char
LexerPeekChar(lexer *Lexer)
{
	char Result = '\0';

	assert(Lexer);
	if(Lexer->Content && Lexer->Content[Lexer->ContentPos])
	{
		Result = Lexer->Content[Lexer->ContentPos];
	}

	return Result;
}

static char
LexerConsumeChar(lexer *Lexer)
{
	char Result = '\0';

	assert(Lexer);
	if(Lexer->Content && Lexer->Content[Lexer->ContentPos])
	{
		Result = Lexer->Content[Lexer->ContentPos];
		Lexer->ContentPos += 1;
	}

	return Result;
}

static void
LexerPushToken(lexer *Lexer, lex_token Token)
{
	lex_token_node *Node = ArrayPush(&Lexer->Arena, lex_token_node, 1);
	Node->Token = Token;
	SLL_QUEUE_PUSH(Lexer->Tokens.Head, Lexer->Tokens.Tail, Node);
	Lexer->Tokens.Count += 1;
}

static void
LexerBuildTokens(lexer *Lexer)
{
	char C = '\0';

	bool Minus = false;

	while((C = LexerConsumeChar(Lexer)))
	{
		if(isdigit(C))
		{
			u32 BytesLeft = 16;
			char *String = ArrayPush(&Lexer->Arena, char, BytesLeft);
			u32 StringPos = 0;

			if(Minus)
			{
				Minus = false;
				String[StringPos++] = C;
				BytesLeft--;
			}

			String[StringPos++] = C;
			BytesLeft--;

			while((C = LexerPeekChar(Lexer)))
			{
				if(isdigit(C))
				{
					if(!BytesLeft)
					{
						BytesPush(&Lexer->Arena, 16);
						BytesLeft = 16;
					}

					String[StringPos++] = C;
					BytesLeft--;

					LexerConsumeChar(Lexer);
				}
				else
				{
					break;
				}
			}

			lex_token Token = {};
			Token.Kind = TokenKind_ImmInt;
			Token.Content = String;
			LexerPushToken(Lexer, Token);
		}
		else if(C == '[')
		{
			lex_token Token = {};
			Token.Kind = TokenKind_BracketOp;
			LexerPushToken(Lexer, Token);
		}
		else if(C == ']')
		{
			lex_token Token = {};
			Token.Kind = TokenKind_BracketCl;
			LexerPushToken(Lexer, Token);
		}
		else if(C == '(')
		{			
			lex_token Token = {};
			Token.Kind = TokenKind_ParenOp;
			LexerPushToken(Lexer, Token);
		}
		else if(C == ')')
		{
			lex_token Token = {};
			Token.Kind = TokenKind_ParenOp;
			LexerPushToken(Lexer, Token);	
		}
		else if(C == '-')
		{
			Minus = true;			
		}
		else if(C == '>')
		{
			assert(Minus);
			Minus = false;

			lex_token Token = {};
			Token.Kind = TokenKind_Arrow;
			LexerPushToken(Lexer, Token);
		}
		else if(C == '.')
		{
			lex_token Token = {};

			char C2 = LexerPeekChar(Lexer);
			if(C2 == '.')
			{
				LexerConsumeChar(Lexer);
				Token.Kind = TokenKind_DotDot;
			}
			else
			{
				Token.Kind = TokenKind_Dot;
			}

			LexerPushToken(Lexer, Token);
		}
		else if(C == '*')
		{
			lex_token Token = {};
			Token.Kind = TokenKind_Star;
			LexerPushToken(Lexer, Token);
		}
		else if(C == '_' || isalpha(C))
		{
			u32 BytesLeft = 16;
			char *String = ArrayPush(&Lexer->Arena, char, BytesLeft);
			u32 StringPos = 0;

			String[StringPos++] = C;
			BytesLeft--;

			while((C = LexerPeekChar(Lexer)))
			{
				if(C == '_' || isalpha(C))
				{
					if(!BytesLeft)
					{
						BytesPush(&Lexer->Arena, 16);
						BytesLeft = 16;
					}

					String[StringPos++] = C;
					BytesLeft--;

					LexerConsumeChar(Lexer);
				}
				else
				{
					break;
				}
			}

			lex_token Token = {};
			Token.Kind = TokenKind_Symbol;
			Token.Content = String;
			LexerPushToken(Lexer, Token);
		}
		else
		{
			assert(false && "Unreachable code");
		}
	}

	lex_token Token = {};
	Token.Kind = TokenKind_EOF;
	LexerPushToken(Lexer, Token);
}

static parser
ParserCreate(lex_token_list *Tokens)
{
	parser Result = {};

	Result.Tokens = Tokens;
	Result.Arena = ArenaCreateZeros(Kilobytes(4));

	return Result;
}

static void	
ParserDestroy(parser *Parser)
{
	ArenaDestroy(&Parser->Arena);
}

static lex_token *
ParserPeekToken(parser *Parser)
{
	return &Parser->PosNode->Token;
}

static lex_token *
ParserConsumeToken(parser *Parser)
{
	lex_token *Result = 0x0;

	if(!Parser->PosNode)
	{
		Parser->PosNode = Parser->Tokens->Head;
		Result = &Parser->PosNode->Token;
	}
	else
	{
		Parser->PosNode = Parser->PosNode->Next;
		Result = &Parser->PosNode->Token;
	}

	return Result;
}

static ast_node *
ParserNextExpression(parser *Parser, ast_node *Prev, token_kind Delimiter)
{
	lex_token *Token = ParserConsumeToken(Parser);
	assert(Token);
	if(Token->Kind == Delimiter)
	{
		return Prev;
	}

	if(!Prev)
	{
		if(Token->Kind == TokenKind_Symbol)
		{
			ast_node *Node = StructPush(&Parser->Arena, ast_node);
			Node->Kind = ASTNodeKind_Ident;
			Node->Token = Token;

			return ParserNextExpression(Parser, Node, Delimiter);
		}
		else if(Token->Kind == TokenKind_ImmInt)
		{
			ast_node *Node = StructPush(&Parser->Arena, ast_node);
			Node->Kind = ASTNodeKind_IntLit;
			Node->Token = Token;

			return ParserNextExpression(Parser, Node, Delimiter);
		}
	}
	else
	{
		if(Token->Kind == TokenKind_BracketOp)
		{
			ast_node *IndexingExpr = ParserNextExpression(Parser, 0x0, TokenKind_BracketCl);
			assert(IndexingExpr->Kind == ASTNodeKind_Ident || IndexingExpr->Kind == ASTNodeKind_IntLit);
			//lex_token *NextToken = ParserConsumeToken(Parser);
			//assert(NextToken->Kind == TokenKind_BracketCl && "Expected a closing brace");

			ast_node *Node = StructPush(&Parser->Arena, ast_node);
			Node->Kind = ASTNodeKind_IndexExpr;
			Node->ChildrenCount = 2;
			Node->Children = ArrayPush(&Parser->Arena, ast_node *, Node->ChildrenCount);

			Node->Children[0] = Prev;
			Node->Children[1] = IndexingExpr;

			return ParserNextExpression(Parser, Node, Delimiter);		
		}
		else
		{
			printf("Unexpected token %s %s\n", LexerTokenKindToString(Token->Kind), Token->Content);
			assert(false);
		}
	}

	return 0x0;
}

static void
ParserBuildAST(parser *Parser)
{
	assert(ParserPeekToken(Parser));

	Parser->AST.Root = ParserNextExpression(Parser, 0x0, TokenKind_EOF);
}
