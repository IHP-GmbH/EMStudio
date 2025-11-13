#!/bin/bash

# Set variables
TARGET_EXE="EMStudio.exe"
DEPLOY_DIR="deployed"
WINDEPLOYQT="/c/Qt/5.15.2/mingw81_64/bin/windeployqt.exe"

# Check if target exists
if [[ ! -f "$TARGET_EXE" ]]; then
  echo "❌ $TARGET_EXE not found in current directory."
  exit 1
fi

# Create deploy directory if not exists
if [[ ! -d "$DEPLOY_DIR" ]]; then
  mkdir "$DEPLOY_DIR"
  echo "✅ Created folder: $DEPLOY_DIR"
fi

# Copy executable
cp "$TARGET_EXE" "$DEPLOY_DIR/"
echo "📦 Copied $TARGET_EXE to $DEPLOY_DIR/"

# Run windeployqt inside the deployed folder
(
  cd "$DEPLOY_DIR" || exit
  echo "🚀 Running windeployqt..."
  "$WINDEPLOYQT" "$TARGET_EXE"
)

echo "✅ Deployment complete."

