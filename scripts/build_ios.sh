#!/usr/bin/env bash
set -euo pipefail

export JUCE_DIR="/Users/luisberna/dev/multitracks/multitracks/third_party/JUCE"
export MTCONTROLS_API_BASE_URL="${MTCONTROLS_API_BASE_URL:-https://playworship-345830648064.us-east1.run.app}"

echo "Generando proyecto Xcode para iOS/iPadOS (Modo RELEASE)..."
cmake --preset ios-release \
  -DMTCONTROLS_API_BASE_URL="$MTCONTROLS_API_BASE_URL" \
  -DCMAKE_BUILD_TYPE=Release

XCODE_PROJ="build/ios-release/MTControls.xcodeproj"

if [[ -d "$XCODE_PROJ" ]]; then
  echo "✅ ¡Proyecto generado con éxito!"
  echo "Abriendo Xcode..."
  open "$XCODE_PROJ"
  echo ""
  echo "⚠️ IMPORTANTE ANTES DE COMPILAR PARA IPAD:"
  echo "1. Conecta tu iPad a la Mac."
  echo "2. Selecciona tu iPad como destino en la barra superior de Xcode."
  echo "3. Haz click en el proyecto 'MTControls' en la barra lateral izquierda."
  echo "4. Ve a 'Signing & Capabilities' y selecciona tu 'Team' de Apple Developer."
  echo "5. Presiona 'Run' (Cmd + R) para compilar e instalar en el iPad."
else
  echo "❌ Error generando el proyecto de Xcode para iOS."
  exit 1
fi
