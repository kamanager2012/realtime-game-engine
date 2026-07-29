#include "poker_engine/phase2/range_visualizer.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace poker_engine {
namespace phase2 {

using namespace poker_engine::range;

namespace {
static const char* RankChar = "23456789TJQKA";
}

RangeVisualizer::RangeVisualizer(const poker_engine::range::Range& range) : range_(range) {}

double RangeVisualizer::GetFrequency(poker_engine::AbstractId id) const {
  uint8_t c1, c2;
  SuitIsomorphism::ToConcrete(id, c1, c2);
  return range_.Get(HandId::Encode(std::min(c1, c2), std::max(c1, c2)));
}

std::string RangeVisualizer::FrequencyToChar(double freq) {
  if (freq > 0.9f) return "██";
  if (freq > 0.75f) return "▓▓";
  if (freq > 0.5f) return "▒▒";
  if (freq > 0.25f) return "░░";
  if (freq > 0.01f) return "··";
  return "  ";
}

std::string RangeVisualizer::FrequencyToColor(double freq) {
  if (freq > 0.9f) return "#ff4444";
  if (freq > 0.75f) return "#ff8800";
  if (freq > 0.5f) return "#ffcc00";
  if (freq > 0.25f) return "#44cc44";
  if (freq > 0.01f) return "#4488ff";
  return "#222222";
}

std::string RangeVisualizer::ToASCII(HandType type) const {
  std::ostringstream oss;

  if (type == HandType::Pairs) {
    oss << "     Pairs (口袋对子)\n";
    oss << "     ";
    for (int r = 12; r >= 0; r--) oss << "  " << RankChar[r] << " ";
    oss << "\n";

    for (int r1 = 12; r1 >= 0; r1--) {
      oss << "  " << RankChar[r1] << "  ";
      for (int r2 = 12; r2 >= 0; r2--) {
        if (r1 == r2) {
          AbstractId id = SuitIsomorphism::ToAbstract(r1 * 4, r1 * 4 + 1);
          double f = GetFrequency(id);
          oss << FrequencyToChar(f) << " ";
        } else {
          oss << "   ";
        }
      }
      oss << "\n";
    }
  } else if (type == HandType::Suited) {
    oss << "     Suited (同花)\n";
    oss << "     ";
    for (int r = 12; r >= 0; r--) oss << "  " << RankChar[r] << " ";
    oss << "\n";

    for (int r1 = 12; r1 >= 0; r1--) {
      oss << "  " << RankChar[r1] << "  ";
      for (int r2 = 12; r2 >= 0; r2--) {
        if (r1 > r2) {
          AbstractId id = SuitIsomorphism::ToAbstract(r1 * 4, r2 * 4);
          double f = GetFrequency(id);
          oss << FrequencyToChar(f) << " ";
        } else if (r1 == r2) {
          oss << "/// ";
        } else {
          AbstractId id = SuitIsomorphism::ToAbstract(r2 * 4, r1 * 4);
          double f = GetFrequency(id);
          oss << FrequencyToChar(f) << " ";
        }
      }
      oss << "\n";
    }
  } else {
    oss << "     Offsuit (杂色)\n";
    oss << "     ";
    for (int r = 12; r >= 0; r--) oss << "  " << RankChar[r] << " ";
    oss << "\n";

    for (int r1 = 12; r1 >= 0; r1--) {
      oss << "  " << RankChar[r1] << "  ";
      for (int r2 = 12; r2 >= 0; r2--) {
        if (r1 > r2) {
          AbstractId id = SuitIsomorphism::ToAbstract(r1 * 4, r2 * 4 + 1);
          double f = GetFrequency(id);
          oss << FrequencyToChar(f) << " ";
        } else if (r1 == r2) {
          oss << "/// ";
        } else {
          AbstractId id = SuitIsomorphism::ToAbstract(r2 * 4, r1 * 4 + 1);
          double f = GetFrequency(id);
          oss << FrequencyToChar(f) << " ";
        }
      }
      oss << "\n";
    }
  }

  oss << "\n  Legend: ██=100% ▓▓>75% ▒▒>50% ░░>25% ··>1%     =0%\n";
  return oss.str();
}

std::string RangeVisualizer::ToFullASCII() const {
  std::ostringstream oss;
  oss << "================== RANGE VISUALIZATION ==================\n\n";
  oss << ToASCII(HandType::Pairs) << "\n\n";
  oss << ToASCII(HandType::Suited) << "\n\n";
  oss << ToASCII(HandType::Offsuit) << "\n";

  int count = range_.NonZeroCount();
  oss << "\nTotal combos: " << count << " / 1326 (" << std::fixed << std::setprecision(1)
      << count / 13.26 << "%)\n";
  return oss.str();
}

std::string RangeVisualizer::ToHTML() const {
  std::ostringstream oss;
  oss << "<!DOCTYPE html><html><head><title>Range Heatmap</title>"
      << "<style>"
      << "body{font-family:monospace;background:#1a1a2e;color:#eee;padding:20px;}"
      << "h1{color:#e94560;}h2{color:#0f3460;}"
      << "table{border-collapse:collapse;margin:10px 0;}"
      << "td{width:36px;height:36px;text-align:center;"
         "font-weight:bold;font-size:14px;border:1px solid #333;}"
      << ".rank{background:#16213e;color:#e94560;font-weight:bold;}"
      << ".label{background:#16213e;color:#aaa;}"
      << "</style></head><body>"
      << "<h1>Poker Range Heatmap</h1>\n";

  const char* titles[] = {"Pairs", "Suited", "Offsuit"};
  for (int t = 0; t < 3; t++) {
    oss << "<h2>" << titles[t] << "</h2><table>\n";
    oss << "<tr><td class='label'></td>";
    for (int r = 12; r >= 0; r--) oss << "<td class='rank'>" << RankChar[r] << "</td>";
    oss << "</tr>\n";

    for (int r1 = 12; r1 >= 0; r1--) {
      oss << "<tr><td class='rank'>" << RankChar[r1] << "</td>";
      for (int r2 = 12; r2 >= 0; r2--) {
        double freq = 0;
        if (t == 0) {
          if (r1 == r2) {
            AbstractId id = SuitIsomorphism::ToAbstract(r1 * 4, r1 * 4 + 1);
            freq = GetFrequency(id);
          }
        } else if (t == 1) {
          if (r1 > r2) {
            AbstractId id = SuitIsomorphism::ToAbstract(r1 * 4, r2 * 4);
            freq = GetFrequency(id);
          } else if (r1 < r2) {
            AbstractId id = SuitIsomorphism::ToAbstract(r2 * 4, r1 * 4);
            freq = GetFrequency(id);
          } else
            freq = -1;
        } else {
          if (r1 > r2) {
            AbstractId id = SuitIsomorphism::ToAbstract(r1 * 4, r2 * 4 + 1);
            freq = GetFrequency(id);
          } else if (r1 < r2) {
            AbstractId id = SuitIsomorphism::ToAbstract(r2 * 4, r1 * 4 + 1);
            freq = GetFrequency(id);
          } else
            freq = -1;
        }

        if (freq < 0) {
          oss << "<td style='background:#0a0a1a;color:#555'>///</td>";
        } else {
          std::string bg = FrequencyToColor(freq);
          oss << "<td style='background:" << bg << ";color:#fff'>" << FrequencyToChar(freq)
              << "</td>";
        }
      }
      oss << "</tr>\n";
    }
    oss << "</table>\n";
  }

  oss << "<p>Combos: " << range_.NonZeroCount() << "/1326</p></body></html>";
  return oss.str();
}

}  // namespace phase2
}  // namespace poker_engine
