msbuild -m build/modupdater.sln /t:UpdaterLib /property:Configuration=Release /property:Platform=x64
msbuild -m build/modupdater.sln /t:UpdaterLib /property:Configuration=Debug /property:Platform=x64
msbuild -m build/modupdater.sln /t:UpdaterLib /property:Configuration=Release /property:Platform=Win32
msbuild -m build/modupdater.sln /t:UpdaterLib /property:Configuration=Debug /property:Platform=Win32