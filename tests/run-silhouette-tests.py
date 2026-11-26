"""Run from a VS x64 Native Tools prompt: python tests/run-silhouette-tests.py."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
app = root / 'MeatyEFTRelease'
def function(source, name):
    start = source.index(name)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

with tempfile.TemporaryDirectory(prefix='meaty-silhouette-') as temporary:
    target = Path(temporary)
    source = (app / 'UI/Appearance.cpp').read_text(encoding='utf-8-sig')
    # Compile the actual production serialization functions, avoiding app startup,
    # DMA, and reads/writes to the user's appearance settings.
    test = '#include "UI/Appearance.h"\n#include "external/nlohmann/json.hpp"\n#include <algorithm>\n#include <cassert>\n#include <iostream>\n'
    test += function(source, 'nlohmann::json PlayerMarkerStyleJson(') + '\n'
    test += function(source, 'void ReadPlayerMarkerStyle(') + '\n'
    test += r'''
int main() {
    PlayerMarkerStyle style;
    ReadPlayerMarkerStyle(nlohmann::json::object(), style, false);
    assert(style.silhouetteModel == SilhouetteModel::Classic);
    style.entitySilhouette = true;
    style.silhouetteOpacity = .37f;
    for (auto model : {SilhouetteModel::Classic, SilhouetteModel::Human}) {
        style.silhouetteModel = model;
        PlayerMarkerStyle restored;
        ReadPlayerMarkerStyle(PlayerMarkerStyleJson(style), restored, false);
        assert(restored.silhouetteModel == model);
        assert(restored.entitySilhouette && restored.silhouetteOpacity == .37f);
    }
    for (const auto bad : {nlohmann::json(-1), nlohmann::json(2), nlohmann::json("Human"), nlohmann::json(nullptr), nlohmann::json(1.5)}) {
        ReadPlayerMarkerStyle({{"silhouetteModel", bad}}, style, false);
        assert(style.silhouetteModel == SilhouetteModel::Classic);
    }
    std::cout << "PASS: model round-trip, old configs, invalid and future model values\n";
}
'''
    (target/'config-test.cpp').write_text(test, encoding='utf-8')
    for name, src in [('geometry', root/'tests/silhouette-model-test.cpp'), ('config', target/'config-test.cpp')]:
        exe = target/(name+'.exe')
        subprocess.run(['cl', '/nologo', '/std:c++20', '/EHsc', '/W4', '/I'+str(app), '/I'+str(app/'external'), str(src), '/Fe:'+str(exe), '/Fo:'+str(target/(name+'.obj'))], cwd=target, check=True)
        subprocess.run([str(exe)], check=True)
