#include <vector>
#include <algorithm>
#include <memory>

#include "DarkChess/core/board.hpp"
#include "DarkChess/core/piece.hpp"
#include "DarkChess/core/utils.hpp"
#include "DarkChess/core/instrumentation.hpp"
#include "DarkChess/core/log.hpp"

namespace DarkChess
{

ChessBoard::ChessBoard() : ChessBoard(false) {}

ChessBoard::ChessBoard(bool t_debug) : m_debug(t_debug)
{
	PROFILE_FUNCTION();

	auto make_piece = [&](ChessPiece cp, int i) {
		std::shared_ptr<ChessPiece> cp_ptr = std::make_shared<ChessPiece>(cp);
		m_board.insert({i, cp_ptr});
		m_moves.insert({cp_ptr, std::make_shared<MoveList>(MoveList())});
		m_own_piece_threats.insert({cp_ptr, std::make_shared<MoveList>(MoveList())});
	};

	// White pawns (1,0) -> (1,7)
	for (int i = 8; i < 16; ++i)
		make_piece(ChessPiece(PieceColour::WHITE, PieceType::PAWN), i);

	// Black pawns (6,0) -> (6,7)
	for (int i = 48; i < 56; ++i)
		make_piece(ChessPiece(PieceColour::BLACK, PieceType::PAWN), i);

	// Knights
	make_piece(ChessPiece(PieceColour::WHITE, PieceType::KNIGHT), 1);
	make_piece(ChessPiece(PieceColour::WHITE, PieceType::KNIGHT), 6);
	make_piece(ChessPiece(PieceColour::BLACK, PieceType::KNIGHT), 57);
	make_piece(ChessPiece(PieceColour::BLACK, PieceType::KNIGHT), 62);

	// Bishops
	make_piece(ChessPiece(PieceColour::WHITE, PieceType::BISHOP), 2);
	make_piece(ChessPiece(PieceColour::WHITE, PieceType::BISHOP), 5);
	make_piece(ChessPiece(PieceColour::BLACK, PieceType::BISHOP), 58);
	make_piece(ChessPiece(PieceColour::BLACK, PieceType::BISHOP), 61);

	// Rooks
	make_piece(ChessPiece(PieceColour::WHITE, PieceType::ROOK), 0);
	make_piece(ChessPiece(PieceColour::WHITE, PieceType::ROOK), 7);
	make_piece(ChessPiece(PieceColour::BLACK, PieceType::ROOK), 56);
	make_piece(ChessPiece(PieceColour::BLACK, PieceType::ROOK), 63);

	// Queens
	make_piece(ChessPiece(PieceColour::WHITE, PieceType::QUEEN), 3);
	make_piece(ChessPiece(PieceColour::BLACK, PieceType::QUEEN), 59);

	// Kings
	make_piece(ChessPiece(PieceColour::WHITE, PieceType::KING), 4);
	make_piece(ChessPiece(PieceColour::BLACK, PieceType::KING), 60);

	generate_moves();
}

int ChessBoard::get_num_moves() const
{
	return m_num_moves;
}

PieceColour ChessBoard::get_turn() const
{
	return m_turn;
}

const std::string ChessBoard::get_turn_name() const
{
	return piece_colour_string.at(m_turn);
}

const std::unique_ptr<BoardMap> ChessBoard::get_pieces() const
{
	return std::make_unique<BoardMap>(m_board);
}

const std::shared_ptr<ChessPiece> ChessBoard::get_piece(int t_ind) const
{
	if (m_board.find(t_ind) == m_board.end())
		return nullptr;
	else
		return m_board.at(t_ind);
}

const std::shared_ptr<ChessPiece> ChessBoard::get_piece(Position t_pos) const
{
	return get_piece(get_index_from_pos(t_pos));
}

void ChessBoard::swap_turn()
{
	m_turn = PieceColour((static_cast<std::underlying_type<PieceColour>::type>(m_turn) + 1) % 2);
}

void ChessBoard::move(int t_from_ind, int t_to_ind)
{
	PROFILE_FUNCTION();

	// If in debug mode then can move any piece anywhere.

	// TODO? raise errors instead of return if can't move

	const std::shared_ptr<ChessPiece> p = get_piece(t_from_ind);

	// check if there is a piece to move
	if (!p)
	{
		DC_CORE_WARN("Invalid move {} to {}: No Piece.",
					 std::to_string(get_pos_from_index(t_from_ind)),
					 std::to_string(get_pos_from_index(t_to_ind)));
		return;
	}

	// check piece is of the correct colour
	// If we're debugging then allow out of turn moves
	if (!m_debug && p->get_colour() != m_turn)
	{
		DC_CORE_WARN("Invalid move {} to {}: Not your turn.",
					 std::to_string(get_pos_from_index(t_from_ind)),
					 std::to_string(get_pos_from_index(t_to_ind)));
		return;
	}

	std::shared_ptr<MoveList> legal_moves = get_moves(p);
	if (!legal_moves)
		DC_CORE_CRITICAL("Piece ({} at {}) has no moves container!",
						 p->get_name(),
						 std::to_string(t_from_ind));

	if (!m_debug && legal_moves && std::find(begin(*legal_moves), end(*legal_moves), t_to_ind) == end(*legal_moves))
	{
		DC_CORE_WARN("Invalid move {} to {}: Not a legal move.",
					 std::to_string(get_pos_from_index(t_from_ind)),
					 std::to_string(get_pos_from_index(t_to_ind)));
		return;
	}

	// Add piece in its new place
	m_board[t_to_ind] = m_board[t_from_ind];

	// remove piece from old position
	m_board.erase(t_from_ind);

	// Swap turns
	swap_turn();
	++m_num_moves;

	DC_CORE_INFO("Move: {} from {} to {}",
				 p->get_name(),
				 std::to_string(get_pos_from_index(t_from_ind)),
				 std::to_string(get_pos_from_index(t_to_ind)));

	// Regenerate possible moves
	generate_moves();
}

void ChessBoard::move(Position t_from_pos, Position t_to_pos)
{
	move(get_index_from_pos(t_from_pos), get_index_from_pos(t_to_pos));
}

void ChessBoard::generate_moves()
{

	PROFILE_FUNCTION();
	for (auto ind_piece_pair : m_board)
	{
		PROFILE_SCOPE("ChessBoard::generate_moves() Loop");

		std::shared_ptr<ChessPiece> current_piece = ind_piece_pair.second;
		int current_ind = ind_piece_pair.first;
		Position current_pos = get_pos_from_index(current_ind);

		if (!current_piece)
		{
			DC_CORE_CRITICAL("Hanging piece pointer ({}) in BoardMap!", std::to_string(current_pos));
			return;
		}

		const std::shared_ptr<MoveList> piece_moves = get_moves(current_piece);
		piece_moves->clear();

		auto add_moves_from_relative = [&](std::vector<Position> relative_moves) {
			for (Position rel_move_pos : relative_moves)
			{
				const Position abs_move_pos = current_pos + rel_move_pos;

				if (out_of_bounds(abs_move_pos))
					continue;

				const std::shared_ptr<ChessPiece> possible_capture = get_piece(abs_move_pos);

				// If there is not a piece, or the piece is capturable
				if (!possible_capture || (possible_capture && possible_capture->get_colour() != current_piece->get_colour()))
				{
					piece_moves->push_back(get_index_from_pos(abs_move_pos));
				}
			}
		};

		switch (current_piece->get_type())
		{
		case PieceType::PAWN:
		{
			// PROFILE_SCOPE("ChessBoard::generate_moves() PAWN");

			//rotation based on colour -> only applies to pawns.
			int rotation = (current_piece->get_colour() == PieceColour::WHITE) ? 1 : -1;

			// Check forward move
			{
				const Position rel_move_pos = {0, 1};
				const Position abs_move_pos = current_pos + rel_move_pos * rotation;

				const std::shared_ptr<ChessPiece> in_front_piece = get_piece(abs_move_pos);

				if (!in_front_piece && !out_of_bounds(abs_move_pos))
					piece_moves->push_back(get_index_from_pos(abs_move_pos));
			}

			// Check capturing moves
			for (Position rel_move_pos : std::vector<Position>{{-1, 1}, {1, 1}})
			{
				const Position abs_move_pos = current_pos + rel_move_pos * rotation;

				if (out_of_bounds(abs_move_pos))
					continue;

				const std::shared_ptr<ChessPiece> possible_capture = get_piece(abs_move_pos);

				if (possible_capture && possible_capture->get_colour() != current_piece->get_colour())
					piece_moves->push_back(get_index_from_pos(abs_move_pos));
			}

			break;
		}

		case PieceType::KNIGHT:
		{
			// PROFILE_SCOPE("ChessBoard::generate_moves() KNIGHT");

			add_moves_from_relative({{1, 2},
									 {2, 1},
									 {-1, 2},
									 {-2, 1},
									 {1, -2},
									 {2, -1},
									 {-1, -2},
									 {-2, -1}});
			break;
		}

		case PieceType::BISHOP:
		{
			// PROFILE_SCOPE("ChessBoard::generate_moves() BISHOP");

			ad_infinitum(current_ind,
						 {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}},
						 piece_moves);
			break;
		}

		case PieceType::ROOK:
		{
			// PROFILE_SCOPE("ChessBoard::generate_moves() ROOK");

			ad_infinitum(current_ind,
						 {{0, 1}, {1, 0}, {0, -1}, {-1, 0}},
						 piece_moves);
			break;
		}

		case PieceType::QUEEN:
		{
			// PROFILE_SCOPE("ChessBoard::generate_moves() QUEEN");

			ad_infinitum(current_ind,
						 {{1, 1},
						  {1, -1},
						  {-1, 1},
						  {-1, -1},
						  {0, 1},
						  {1, 0},
						  {0, -1},
						  {-1, 0}},
						 piece_moves);
			break;
		}

		case PieceType::KING:
		{
			// PROFILE_SCOPE("ChessBoard::generate_moves() KING");

			add_moves_from_relative({{1, 1},
									 {1, -1},
									 {-1, 1},
									 {-1, -1},
									 {0, 1},
									 {1, 0},
									 {0, -1},
									 {-1, 0}});

			break;
		}

		default:
			// TODO raise error
			DC_CORE_CRITICAL("Unknown Piece.");
			break;
		}
	}

	prune_moves();
}

void ChessBoard::prune_moves()
{

	PROFILE_FUNCTION();

	// Prune relevant moves for pinned pieces
	for (auto map_entry : m_pinned_pieces)
	{
		Position pinned_pos = map_entry.first;
		Position pinned_by_pos = map_entry.second;
		std::shared_ptr<ChessPiece> pinned_piece = get_piece(pinned_pos);

		std::shared_ptr<MoveList> pinned_moves = get_moves(pinned_pos);

		if (!pinned_moves)
		{
			DC_CORE_CRITICAL("{} at {} has no moves container.", pinned_piece->get_name(), std::to_string(pinned_pos));
			continue;
		}

		std::shared_ptr<MoveList> new_moves = std::make_shared<MoveList>();

		for (int move : *pinned_moves)
		{
			Position move_pos = get_pos_from_index(move);
			if (is_colinear(move_pos, pinned_pos, pinned_by_pos))
			{
				//DC_CORE_TRACE("{} is colinear to {} and {}, it is a valid move.", std::to_string(move_pos), std::to_string(pinned_pos), std::to_string(pinned_by_pos));
				new_moves->push_back(move);
			}
		}

		m_moves[pinned_piece] = new_moves;
		//DC_CORE_TRACE("{} now only has {} moves.", pinned_piece->get_name(), get_moves(pinned_piece)->size());
	}

	m_pinned_pieces.clear();

	// Prune relevant moves for king

	std::shared_ptr<ChessPiece> kings[2];
	MoveList threatening_moves[2];

	// Get both the king and the moves that they can't go to.
	for (auto m : m_moves)
	{

		std::shared_ptr<ChessPiece> attacking_piece = m.first;
		std::shared_ptr<MoveList> attacking_moves = m.second;
		int index = static_cast<std::underlying_type<PieceColour>::type>(attacking_piece->get_colour());

		if (attacking_piece->get_type() == PieceType::KING)
			kings[index] = attacking_piece;
		else
		{
			threatening_moves[index].reserve(threatening_moves[index].size() + attacking_moves->size());
			threatening_moves[index].insert(end(threatening_moves[index]), begin(*attacking_moves), end(*attacking_moves));
		}
	}

	// Remove all the moves the kings can't go to from their move list.
	for (int i = 0; i < 2; ++i)
	{
		std::shared_ptr<ChessPiece> king = kings[i];
		std::shared_ptr<MoveList> king_move_list = get_moves(king);

		if (!king_move_list)
		{
			DC_CORE_CRITICAL("{} has no moves container.", king->get_name());
			break;
		}

		// std::set_difference needs sorted containers
		std::sort(begin(*king_move_list), end(*king_move_list));
		std::sort(begin(threatening_moves[1 - i]), end(threatening_moves[1 - i]));

		MoveList new_king_moves;
		std::set_difference(begin(*king_move_list),
							end(*king_move_list),
							begin(threatening_moves[1 - i]),
							end(threatening_moves[1 - i]),
							std::inserter(new_king_moves, begin(new_king_moves)));

		m_moves[king] = std::make_shared<MoveList>(new_king_moves);
	}
}

void ChessBoard::ad_infinitum(int t_ind, std::vector<Position> t_directions, std::shared_ptr<MoveList> t_movelist)
{
	PROFILE_FUNCTION();

	const Position current_pos = get_pos_from_index(t_ind);
	const std::shared_ptr<ChessPiece> current_piece = get_piece(t_ind);

	for (Position dir : t_directions)
	{
		bool looking_for_king = false;
		std::shared_ptr<ChessPiece> potentially_pinned_piece = nullptr;
		Position potentially_pinned_piece_pos;

		// There can only be a max of 7 iterations before going out of bounds
		// start at 1 since 0 will be the current position.
		for (int i = 1; i < 8; ++i)
		{
			const Position to_pos = current_pos + dir * i;

			if (out_of_bounds(to_pos))
				break;

			const std::shared_ptr<ChessPiece> to_piece = get_piece(to_pos);

			bool is_enemy_piece = to_piece && to_piece->get_colour() != current_piece->get_colour();

			if (looking_for_king)
			{
				// Found enemy king with no blockers -> piece is pinned.
				if (is_enemy_piece && to_piece->get_type() == PieceType::KING)
				{
					DC_CORE_TRACE("{} is pinned!", potentially_pinned_piece->get_name());
					m_pinned_pieces.insert({potentially_pinned_piece_pos, current_pos});
				}
				else if (to_piece)
				{
					// found a blocker (either colour) -> piece is not pinned.
					break;
				}
				// no piece found yet, keep looking.
			}
			else
			{

				if (!to_piece)
				{
					// Found no piece: Keep looking in this direction.
					t_movelist->push_back(get_index_from_pos(to_pos));
					continue;
				}
				else if (is_enemy_piece)
				{
					// Found enemy piece: Stop looking for moves & now look to see if piece is pinned.
					t_movelist->push_back(get_index_from_pos(to_pos));
					//DC_CORE_TRACE("Checking if {} is pinned by the {}", to_piece->get_name(), current_piece->get_name());
					looking_for_king = true;
					potentially_pinned_piece = to_piece;
					potentially_pinned_piece_pos = to_pos;
					continue;
				}
				else
				{
					// Found our own piece
					if (m_own_piece_threats.find(current_piece) == m_own_piece_threats.end())
					{
						DC_CORE_ERROR("Moves being generated for {} but no container exists for own piece threats. Creating new container.");
						m_own_piece_threats.insert({current_piece, std::make_shared<MoveList>(MoveList())});
					}
					m_own_piece_threats.at(current_piece)->push_back(get_index_from_pos(to_pos));
					break;
				}
			}
		}
	}
}

const std::shared_ptr<MoveList> ChessBoard::get_moves(std::shared_ptr<ChessPiece> t_cp) const
{
	if (!t_cp || m_moves.find(t_cp) == m_moves.end())
		return nullptr;
	else
		return m_moves.at(t_cp);
}

const std::shared_ptr<MoveList> ChessBoard::get_moves(int t_ind) const
{
	return get_moves(get_piece(t_ind));
}

const std::shared_ptr<MoveList> ChessBoard::get_moves(Position t_pos) const
{
	return get_moves(get_index_from_pos(t_pos));
}

std::string ChessBoard::to_string() const
{
	PROFILE_FUNCTION();

	std::string str;

	str += "\n  ---------------------------------\n";

	for (int y = 7; y >= 0; y--)
	{
		str += std::to_string(y) + " ";
		for (int x = 0; x < 8; x++)
		{
			char sym;
			std::shared_ptr<ChessPiece> cp = get_piece({x, y});

			if (cp)
				sym = cp->get_symbol();
			else
				sym = ' ';

			str += "| ";
			str.push_back(sym);
			str += " ";
		}
		str += "|\n  ---------------------------------\n";
	}

	str += "    0   1   2   3   4   5   6   7  \n";

	return str;
}

} // namespace DarkChess