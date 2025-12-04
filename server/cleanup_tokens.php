<?php
/*
 * OpenAI Proxy Server with context window and web search for FujiNet AI SAM App
 * 
 * Copyright (c) 2025 Joe Honold, mozzwald <gmail.com>
 * 
 * GPL v3 License
 * ------------- cleanup_tokens.php
 * - Calculates "last activity" per token as:
 *      COALESCE(MAX(messages.created_at), tokens.created_at)
 * - Deletes tokens whose last activity is older than $daysLimit days
 * - Deletes all messages belonging to those tokens
 * - Only actually runs once per $minIntervalSeconds (pseudo cron)
 *
 * Usage examples:
 *   php cleanup_tokens.php
 *   php cleanup_tokens.php 14    # keep 14 days instead of default
 */

include_once "includes.php";

/* ---------- Config ---------- */

// Allow optional override via CLI arg
if (isset($argv[1]) && is_numeric($argv[1])) {
    $daysLimit = max(1, (int)$argv[1]);
}

// Minimum interval between actual cleanups (pseudo cron) — 1 day
$minIntervalSeconds = 86400;

// File used to track last run time
$lastRunFile = "cleanup_tokens_last_run.txt";

/* ---------- Pseudo cron guard ---------- */

$now = time();
if (file_exists($lastRunFile)) {
    $lastRun = filemtime($lastRunFile);
    if ($lastRun !== false && ($now - $lastRun) < $minIntervalSeconds) {
        // Too soon since last cleanup; just exit quietly.
        exit;
    }
}

// Update the last-run file *early* to avoid multiple parallel runs
@file_put_contents($lastRunFile, date("c"));

/* ---------- DB connection ---------- */

$mysqli = @new mysqli($dbhost, $dbuser, $dbpass, $dbname);
if ($mysqli->connect_errno) {
    if ($log_errors) {
        file_put_contents(
            $log_file,
            date("[Y-m-d H:i:s]") . " cleanup_tokens.php DB connect error: " . $mysqli->connect_error . "\n",
            FILE_APPEND
        );
    }
    exit(1);
}
$mysqli->set_charset("utf8mb4");

/* ---------- Compute cutoff datetime ---------- */

$cutoffDate = (new DateTimeImmutable("now"))
    ->modify("-{$daysLimit} days")
    ->format("Y-m-d H:i:s");

/*
 * We define "old tokens" as those whose last activity is older than $cutoffDate.
 *
 * last_activity(token) = COALESCE(MAX(m.created_at), t.created_at)
 *
 * We’ll use this definition in a subquery and then:
 * - Delete messages for those tokens
 * - Delete the tokens themselves
 */

/* ---------- Delete messages for old tokens ---------- */

$deletedMessages = 0;
$deletedTokens   = 0;

$sqlDeleteMessages = "
    DELETE m
    FROM messages AS m
    JOIN (
        SELECT
            t.token_id
        FROM tokens AS t
        LEFT JOIN (
            SELECT token_id, MAX(created_at) AS last_msg_at
            FROM messages
            GROUP BY token_id
        ) AS lm ON lm.token_id = t.token_id
        WHERE COALESCE(lm.last_msg_at, t.created_at) < ?
    ) AS old_tokens ON m.token_id = old_tokens.token_id
";

if ($stmt = $mysqli->prepare($sqlDeleteMessages)) {
    $stmt->bind_param("s", $cutoffDate);
    if ($stmt->execute()) {
        $deletedMessages = $stmt->affected_rows;
    } else {
        if ($log_errors) {
            file_put_contents(
                $log_file,
                date("[Y-m-d H:i:s]") . " cleanup_tokens.php error deleting messages: " . $stmt->error . "\n",
                FILE_APPEND
            );
        }
    }
    $stmt->close();
} else {
    if ($log_errors) {
        file_put_contents(
            $log_file,
            date("[Y-m-d H:i:s]") . " cleanup_tokens.php prepare error (messages): " . $mysqli->error . "\n",
            FILE_APPEND
        );
    }
}

/* ---------- Delete old tokens themselves ---------- */

$sqlDeleteTokens = "
    DELETE t
    FROM tokens AS t
    LEFT JOIN (
        SELECT token_id, MAX(created_at) AS last_msg_at
        FROM messages
        GROUP BY token_id
    ) AS lm ON lm.token_id = t.token_id
    WHERE COALESCE(lm.last_msg_at, t.created_at) < ?
";

if ($stmt = $mysqli->prepare($sqlDeleteTokens)) {
    $stmt->bind_param("s", $cutoffDate);
    if ($stmt->execute()) {
        $deletedTokens = $stmt->affected_rows;
    } else {
        if ($log_errors) {
            file_put_contents(
                $log_file,
                date("[Y-m-d H:i:s]") . " cleanup_tokens.php error deleting tokens: " . $stmt->error . "\n",
                FILE_APPEND
            );
        }
    }
    $stmt->close();
} else {
    if ($log_errors) {
        file_put_contents(
            $log_file,
            date("[Y-m-d H:i:s]") . " cleanup_tokens.php prepare error (tokens): " . $mysqli->error . "\n",
            FILE_APPEND
        );
    }
}

$mysqli->close();

/* ---------- Log summary ---------- */

if ($log_errors) {
    file_put_contents(
        $log_file,
        date("[Y-m-d H:i:s]") .
        " cleanup_tokens.php ran: daysLimit={$daysLimit}, cutoff={$cutoffDate}, " .
        "deletedMessages={$deletedMessages}, deletedTokens={$deletedTokens}\n",
        FILE_APPEND
    );
}

exit(0);