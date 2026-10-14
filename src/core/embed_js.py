import pathlib import sys

    output = pathlib.Path(sys.argv[1]) sources =[pathlib.Path(p) for p in sys.argv[2 : ]]

                                                 lines =["#pragma once", "#include <string_view>", "namespace edon::core {", ]

                                                         for source in sources : name = source.stem content = source.read_text(encoding = "utf-8") delimiter = "EDONJS" while f "){delimiter}\"" in content : delimiter += "_"

                                                                                                                               lines.append(f 'inline constexpr std::string_view {name} = ' f 'R"{delimiter}({content}){delimiter}";')

                                                                                                                                                lines.append("}")

                                                                                                                                                                 output.parent.mkdir(parents = True, exist_ok = True) output.write_text("\n".join(lines) + "\n", encoding = "utf-8")
