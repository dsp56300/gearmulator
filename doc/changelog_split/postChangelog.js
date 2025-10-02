#!/usr/bin/env node
import fs from "fs";
import fetch from "node-fetch";

// Usage: node postChangelog.js <webhookUrl> <threadId> <filePath>

if (process.argv.length < 5) {
	console.error("Usage: node postChangelog.js <webhookUrl> <threadId> <filePath>");
	process.exit(1);
}

const [,, webhookUrl, threadId, filePath] = process.argv;

async function postChangelog() {
	// Check if file exists
	if (!fs.existsSync(filePath)) {
		console.log(`⚠️ Skipping: file "${filePath}" not found`);
		process.exit(0); // exit gracefully
	}

	// Read changelog from file
	const changelog = fs.readFileSync(filePath, "utf-8").trim();
	if (!changelog) {
		console.log(`⚠️ Skipping: file "${filePath}" is empty`);
		process.exit(0);
	}

	// Post to Discord
	const response = await fetch(`${webhookUrl}?thread_id=${threadId}`, {
		method: "POST",
		headers: { "Content-Type": "application/json" },
		body: JSON.stringify({ content: changelog }),
	});

	if (!response.ok) {
		console.error("❌ Failed to post:", response.status, await response.text());
		process.exit(1);
	}

	console.log("✅ Changelog posted successfully");
}

postChangelog().catch(err => {
	console.error("❌ Error:", err);
	process.exit(1);
});
