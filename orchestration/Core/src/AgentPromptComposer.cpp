#include "VexaraOrchestration/AgentPromptComposer.h"



#include "VexaraCore/GrokTaskContext.h"

#include <QJsonDocument>



namespace VexaraOrchestration {

namespace {

QString summarizeApprovedPlanForAider(const QJsonObject& approvedPlanPayload)
{
    QStringList lines;
    const QString summary = approvedPlanPayload.value(QStringLiteral("summary")).toString().trimmed();
    if (!summary.isEmpty()) {
        lines.append(summary);
    }

    const QJsonArray subtasks = approvedPlanPayload.value(QStringLiteral("subtasks")).toArray();
    for (const QJsonValue& subtaskValue : subtasks) {
        const QJsonObject subtask = subtaskValue.toObject();
        const QString id = subtask.value(QStringLiteral("id")).toString().trimmed();
        const QString title = subtask.value(QStringLiteral("title")).toString().trimmed();
        const QString description = subtask.value(QStringLiteral("description")).toString().trimmed();

        QStringList paths;
        const QJsonArray targetFiles = subtask.value(QStringLiteral("target_files")).toArray();
        for (const QJsonValue& fileValue : targetFiles) {
            const QString path = fileValue.toString().trimmed();
            if (!path.isEmpty()) {
                paths.append(path);
            }
        }

        QString line;
        if (!id.isEmpty()) {
            line += id + QStringLiteral(": ");
        }
        if (!title.isEmpty()) {
            line += title;
        }
        if (!description.isEmpty()) {
            line += QStringLiteral(" — ") + description;
        }
        if (!paths.isEmpty()) {
            line += QStringLiteral(" [") + paths.join(QStringLiteral(", ")) + QLatin1Char(']');
        }
        if (!line.trimmed().isEmpty()) {
            lines.append(line.trimmed());
        }
    }

    if (lines.isEmpty()) {
        return QStringLiteral("(no structured plan summary)");
    }
    return lines.join(QStringLiteral("\n"));
}

} // namespace

void AgentPromptComposer::configure(const VexaraCore::GrokBuildSettings& grokBuild,

                                  const VexaraCore::OpenClawSettings& openClaw)

{

    grokBuild_ = grokBuild;

    openClaw_ = openClaw;

}



QString AgentPromptComposer::composeUserPrompt(AgentRole role,

                                               const QString& userTask,

                                               const TaskContext& context) const

{

    switch (role) {

    case AgentRole::Orchestrator:

        return composeOrchestratorPrompt(userTask, context);

    case AgentRole::Supervisor:

        return composeSupervisorPrompt(userTask, context);

    case AgentRole::Builder:

        return grokBuild_.composePrompt(userTask, context);

    }

    return userTask;

}



QString AgentPromptComposer::composeOrchestratorPrompt(const QString& userTask,

                                                       const TaskContext& context) const

{

    return openClaw_.composePrompt(userTask, context.projectPath);

}



QString AgentPromptComposer::composeSupervisorPrompt(const QString& userTask,

                                                     const TaskContext& context) const

{

    QString prompt = QStringLiteral(

        "You are the Supervisor agent in Vexara. Review the following task request and outline "

        "risks, verification steps, and acceptance criteria.\n\nTask:\n%1")

                         .arg(userTask);

    if (!context.projectPath.isEmpty()) {

        prompt += QStringLiteral("\n\nProject root: %1").arg(context.projectPath);

    }

    return prompt;

}



QString AgentPromptComposer::systemContextFor(AgentRole role, const TaskContext& context) const

{

    QString system;

    switch (role) {

    case AgentRole::Orchestrator:

        system = QStringLiteral(

            "You are the Orchestrator agent in Vexara. Produce a concise plan and coordination "

            "notes for the Builder. Do not execute shell commands.");

        break;

    case AgentRole::Supervisor:

        system = QStringLiteral(

            "You are the Supervisor agent in Vexara. Focus on review, verification, and quality "

            "checks.");

        break;

    case AgentRole::Builder:

        system = QStringLiteral(

            "You are the Builder agent inside Vexara. Execute the task described in the user "

            "message with actionable guidance and concrete steps.");

        break;

    }



    if (!context.projectPath.isEmpty()) {

        system += QStringLiteral("\n\nProject root: %1").arg(context.projectPath);

    }

    if (!context.currentFilePath.isEmpty()) {

        system += QStringLiteral("\nOpen file: %1").arg(context.currentFilePath);

    }

    if (!context.selectedText.isEmpty()) {

        system += QStringLiteral("\nSelected context:\n") + context.selectedText;

    }

    return system;

}

QString AgentPromptComposer::composeBuilderPromptWithPlan(const QString& userTask,
                                                          const QString& orchestratorPlan,
                                                          const TaskContext& context) const
{
    QString prompt = composeUserPrompt(AgentRole::Builder, userTask, context);
    if (!orchestratorPlan.trimmed().isEmpty()) {
        prompt = QStringLiteral("## Orchestrator plan\n%1\n\n## Build task\n%2")
                     .arg(orchestratorPlan.trimmed(), prompt);
    }
    return prompt;
}

QString AgentPromptComposer::composeSupervisorReviewPrompt(const QString& userTask,
                                                           const QString& builderResult,
                                                           const TaskContext& context) const
{
    QString prompt = QStringLiteral(
        "You are the Supervisor agent in Vexara. Review the Builder output for the task below. "
        "State whether the work is acceptable, list issues, and suggest verification steps.\n\n"
        "## Original task\n%1\n\n## Builder output\n%2")
                         .arg(userTask, builderResult.trimmed().isEmpty()
                                              ? QStringLiteral("(no builder output)")
                                              : builderResult);
    if (!context.projectPath.isEmpty()) {
        prompt += QStringLiteral("\n\nProject root: %1").arg(context.projectPath);
    }
    return prompt;
}

QString AgentPromptComposer::composePlannerPrompt(const QString& userTask,
                                                  const TaskContext& context) const
{
    const QString projectRoot = context.projectPath.trimmed().isEmpty()
                                    ? QStringLiteral("(no project open)")
                                    : context.projectPath.trimmed();
    const QString projectType = context.detectedProjectType.trimmed().isEmpty()
                                  ? QStringLiteral("(unknown)")
                                  : context.detectedProjectType.trimmed();

    QString prompt = QStringLiteral(
        "You are the Planner agent in Vexara.\n"
        "Your ONLY job is to output a SINGLE valid JSON object. "
        "Do not include any explanations, markdown, code, or extra text.\n\n"

        "=== PROJECT CONTEXT ===\n"
        "Project root: %1\n"
        "Project type: %2\n\n"

        "=== USER TASK ===\n"
        "%3\n\n"

        "=== INSTRUCTIONS ===\n"
        "Break the task into clear, ordered subtasks.\n"
        "For each subtask provide a title, description, and acceptance criteria.\n"
        "Also provide overall acceptance criteria and any risk notes.\n\n"

        "=== REQUIRED JSON OUTPUT SHAPE ===\n"
        "{\n"
        "  \"summary\": \"one paragraph overview\",\n"
        "  \"subtasks\": [\n"
        "    {\n"
        "      \"id\": \"1\",\n"
        "      \"title\": \"...\",\n"
        "      \"description\": \"...\",\n"
        "      \"acceptance_criteria\": [\"...\", \"...\"]\n"
        "    }\n"
        "  ],\n"
        "  \"acceptance_criteria\": [\"overall criteria 1\", \"overall criteria 2\"],\n"
        "  \"risk_notes\": [\"risk or mitigation note\"]\n"
        "}\n\n"

        "CRITICAL RULES:\n"
        "- Output ONLY the JSON object. Nothing else.\n"
        "- The response must start with '{' and end with '}'.\n"
        "- Do not repeat any of these instructions.\n"
        "- Do not add markdown fences (```json) or any commentary.\n"
        "- If you cannot follow these rules, output the minimal valid JSON anyway.\n")
                         .arg(projectRoot, projectType, userTask.trimmed());

    return prompt;
}

QString AgentPromptComposer::composeWorkerPrompt(const QString& userTask,
                                                 const QJsonObject& approvedPlanPayload,
                                                 const QString& workerInstructions,
                                                 const TaskContext& context,
                                                 bool structuredEditsExpected,
                                                 VexaraCore::AgentServiceKind cliBackendKind) const
{
    if (cliBackendKind == VexaraCore::AgentServiceKind::AiderCli) {
        const QString planSection = summarizeApprovedPlanForAider(approvedPlanPayload);
        QString prompt =
            QStringLiteral("Make a minimal code change. Do not explain at length.\n\n"
                           "Task: %1\n")
                .arg(userTask.trimmed());
        if (!workerInstructions.trimmed().isEmpty()) {
            prompt += QStringLiteral("Instructions: %1\n").arg(workerInstructions.trimmed());
        }
        if (!planSection.trimmed().isEmpty()
            && planSection != QStringLiteral("(no structured plan summary)")) {
            prompt += QStringLiteral("Plan:\n%1\n").arg(planSection);
        }
        if (!context.targetFiles.isEmpty()) {
            prompt += QStringLiteral("\nEdit ONLY these files:\n");
            for (const QString& file : context.targetFiles) {
                prompt += QStringLiteral("- %1\n").arg(file);
            }
        }
        prompt += QStringLiteral(
            "\nApply the change directly. Do not modify config, CMake, scripts, or other files.");
        return prompt;
    }

    const QJsonDocument planDoc(approvedPlanPayload);
    QString prompt = QStringLiteral(
        "You are the Worker agent in Vexara — the coding engine.\n"
        "Execute the approved plan with precise, minimal changes.\n\n"
        "### Original task\n%1\n\n"
        "### Supervisor worker instructions\n%2\n\n"
        "### Approved plan\n%3\n")
                         .arg(userTask.trimmed(),
                              workerInstructions.trimmed().isEmpty()
                                  ? QStringLiteral("(none)")
                                  : workerInstructions.trimmed(),
                              QString::fromUtf8(planDoc.toJson(QJsonDocument::Compact)));

    if (!context.projectPath.isEmpty()) {
        prompt += QStringLiteral("\n\n### Project root\n%1").arg(context.projectPath);
    }

    if (structuredEditsExpected) {
        prompt += QStringLiteral(
            "\n\nRespond with a single JSON object (no markdown fences):\n"
            "{\n"
            "  \"summary\": \"what you changed and why\",\n"
            "  \"edits\": [\n"
            "    {\"path\": \"relative/or/absolute/file\", \"line\": 1, \"column\": 1, "
            "\"replace_length\": 0, \"new_text\": \"...\"}\n"
            "  ]\n"
            "}\n"
            "Rules:\n"
            "- Prefer small, targeted edits over rewriting whole files.\n"
            "- Use project-relative paths when possible.\n"
            "- line/column are 1-based.\n"
            "- Output valid JSON only.");
    } else {
        prompt += QStringLiteral(
            "\n\nYou are running through a CLI coding backend with filesystem access.\n"
            "Make the required changes directly in the project directory.\n"
            "When finished, summarize what you changed and why.");
    }

    return prompt;
}

QString AgentPromptComposer::composeSupervisorPlanReviewPrompt(const QString& userTask,
                                                                 const QJsonObject& planPayload,
                                                                 const QJsonArray& availableBackends,
                                                                 const TaskContext& context) const
{
    const QJsonDocument backendsDoc(availableBackends);
    const QJsonDocument planDoc(planPayload);

    QString prompt = QStringLiteral(
        "You are the Supervisor agent in Vexara — the quality gate and model router.\n"
        "Review the Planner output below. Evaluate clarity, completeness, and risk.\n"
        "Choose the best Worker backend from the available list and refine instructions "
        "for execution.\n\n"
        "Respond with a single JSON object (no markdown fences):\n"
        "{\n"
        "  \"decision\": \"approve|rework|defer\",\n"
        "  \"confidence\": 0.0,\n"
        "  \"reasoning\": \"why you chose this path\",\n"
        "  \"chosen_backend\": \"backend id from available_backends\",\n"
        "  \"plan_issues\": [\"gaps or problems in the plan\"],\n"
        "  \"risk_assessment\": \"residual risks after review\",\n"
        "  \"worker_instructions\": \"refined, actionable instructions for the Worker\",\n"
        "  \"approved_plan\": {\n"
        "    \"summary\": \"...\",\n"
        "    \"target_files\": [\"relative/path/to/file.cpp\"],\n"
        "    \"subtasks\": [{\"id\":\"1\",\"title\":\"...\",\"description\":\"...\","
        "\"target_files\":[\"relative/path/to/file.cpp\"],"
        "\"acceptance_criteria\":[\"...\"]}],\n"
        "    \"acceptance_criteria\": [\"...\"]\n"
        "  }\n"
        "}\n\n"
        "### Original task\n%1\n\n"
        "### Planner plan (JSON)\n%2\n\n"
        "### Available Worker backends\n%3")
                         .arg(userTask.trimmed(),
                              QString::fromUtf8(planDoc.toJson(QJsonDocument::Compact)),
                              QString::fromUtf8(backendsDoc.toJson(QJsonDocument::Compact)));

    if (!context.projectPath.isEmpty()) {
        prompt += QStringLiteral("\n\n### Project root\n%1").arg(context.projectPath);
    }
    if (!context.detectedProjectType.isEmpty()) {
        prompt += QStringLiteral("\n\n### Project type\n%1").arg(context.detectedProjectType);
    }

    prompt += QStringLiteral(
        "\n\nRules:\n"
        "- Prefer CLI backends (aider_cli, grok_cli) when the task requires reading or editing files.\n"
        "- Prefer aider_cli for free/local Ollama models with strong git-aware editing.\n"
        "- Include target_files in approved_plan with ONLY the 1-2 source files the Worker "
        "must edit (never config, docs, or reference files).\n"
        "- Use HTTP backends only for text-only guidance.\n"
        "- Set decision=rework if the plan is vague, incomplete, or unsafe.\n"
        "- Set decision=defer only for blocking external dependencies.\n"
        "- confidence is 0.0-1.0 (use < 0.55 when uncertain).\n"
        "- Output valid JSON only.");
    return prompt;
}

QString AgentPromptComposer::composeTesterEvaluationPrompt(const QString& userTask,
                                                           const QJsonObject& approvedPlanPayload,
                                                           const QString& workerResultMarkdown,
                                                           const QJsonArray& commandResults,
                                                           const TaskContext& context) const
{
    const QJsonDocument planDoc(approvedPlanPayload);
    const QJsonDocument commandDoc(commandResults);

    QString prompt = QStringLiteral(
        "You are the Tester agent in Vexara — a verification specialist.\n"
        "Evaluate whether the Worker output satisfies the approved plan and acceptance criteria.\n"
        "Use the captured command output as ground truth for build/test/runtime behavior.\n\n"
        "Respond with a single JSON object (no markdown fences):\n"
        "{\n"
        "  \"overall_verdict\": \"pass|fail\",\n"
        "  \"summary\": \"short overall result\",\n"
        "  \"reasoning\": \"why you chose pass or fail\",\n"
        "  \"subtask_results\": [\n"
        "    {\"id\": \"1\", \"title\": \"...\", \"passed\": true, \"notes\": \"...\"}\n"
        "  ],\n"
        "  \"issues\": [\"concrete problems found\"]\n"
        "}\n\n"
        "### Original task\n%1\n\n"
        "### Approved plan (JSON)\n%2\n\n"
        "### Worker result\n%3\n\n"
        "### Command runs (JSON)\n%4")
                         .arg(userTask.trimmed(),
                              QString::fromUtf8(planDoc.toJson(QJsonDocument::Compact)),
                              workerResultMarkdown.trimmed().isEmpty()
                                  ? QStringLiteral("(no worker summary)")
                                  : workerResultMarkdown.trimmed(),
                              QString::fromUtf8(commandDoc.toJson(QJsonDocument::Compact)));

    if (!context.projectPath.isEmpty()) {
        prompt += QStringLiteral("\n\n### Project root\n%1").arg(context.projectPath);
    }
    if (!context.detectedProjectType.isEmpty()) {
        prompt += QStringLiteral("\n\n### Project type\n%1").arg(context.detectedProjectType);
    }

    prompt += QStringLiteral(
        "\n\nRules:\n"
        "- Mark overall_verdict=fail if any command run failed unless clearly unrelated.\n"
        "- Evaluate each subtask against its acceptance_criteria when present.\n"
        "- List concrete issues with file/symbol references when possible.\n"
        "- Do not assume success without evidence in command output or worker result.\n"
        "- Output valid JSON only.");
    return prompt;
}

QString AgentPromptComposer::composeReviewerPrompt(const QString& userTask,
                                                   const QJsonObject& approvedPlanPayload,
                                                   const QString& workerResultMarkdown,
                                                   const QJsonObject& testResultsPayload,
                                                   const TaskContext& context) const
{
    const QJsonDocument planDoc(approvedPlanPayload);
    const QJsonDocument testDoc(testResultsPayload);

    QString prompt = QStringLiteral(
        "You are the Reviewer agent in Vexara — the final quality gate.\n"
        "Perform a human-like review of the full pipeline outcome: approved plan, worker "
        "execution, and test verification.\n"
        "Decide whether the task is truly complete and shippable.\n\n"
        "Respond with a single JSON object (no markdown fences):\n"
        "{\n"
        "  \"decision\": \"approve|rework|escalate\",\n"
        "  \"confidence\": 0.0,\n"
        "  \"reasoning\": \"detailed rationale\",\n"
        "  \"rework_stage\": \"planner|supervisor|worker|tester\",\n"
        "  \"summary\": \"short outcome summary\",\n"
        "  \"issues\": [\"concrete remaining problems\"]\n"
        "}\n\n"
        "### Original task\n%1\n\n"
        "### Approved plan (JSON)\n%2\n\n"
        "### Worker result\n%3\n\n"
        "### Test results (JSON)\n%4")
                         .arg(userTask.trimmed(),
                              QString::fromUtf8(planDoc.toJson(QJsonDocument::Compact)),
                              workerResultMarkdown.trimmed().isEmpty()
                                  ? QStringLiteral("(no worker summary)")
                                  : workerResultMarkdown.trimmed(),
                              QString::fromUtf8(testDoc.toJson(QJsonDocument::Compact)));

    if (!context.projectPath.isEmpty()) {
        prompt += QStringLiteral("\n\n### Project root\n%1").arg(context.projectPath);
    }
    if (!context.detectedProjectType.isEmpty()) {
        prompt += QStringLiteral("\n\n### Project type\n%1").arg(context.detectedProjectType);
    }

    prompt += QStringLiteral(
        "\n\nRules:\n"
        "- Use decision=approve only when acceptance criteria are met and tests support success.\n"
        "- Use decision=rework when a specific earlier stage should rerun; set rework_stage accordingly.\n"
        "  - planner: plan was wrong or incomplete\n"
        "  - supervisor: routing/plan approval needs re-evaluation\n"
        "  - worker: code changes are insufficient or incorrect\n"
        "  - tester: verification was inadequate or needs re-run after fixes\n"
        "- Use decision=escalate when the pipeline needs Director-level re-orchestration "
        "(conflicting goals, blocked dependencies, repeated failures).\n"
        "- confidence is 0.0-1.0; use values below 0.55 when uncertain.\n"
        "- Be specific in issues — reference files, tests, or criteria.\n"
        "- Output valid JSON only.");
    return prompt;
}

} // namespace VexaraOrchestration


