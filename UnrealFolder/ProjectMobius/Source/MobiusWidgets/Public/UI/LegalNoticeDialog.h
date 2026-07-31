// Copyright (c) 2026 ProjectMobius contributors. MIT License.

#pragma once

class UUserProjectSettings;

/** Native, application-modal first-launch legal notice for packaged desktop builds. */
namespace MobiusLegalNotice
{
	void ShowIfRequired(UUserProjectSettings& UserSettings);

	/** Opens the notice regardless of saved acceptance. Development visual-review aid. */
	void ShowPreview(UUserProjectSettings& UserSettings);
}
