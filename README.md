# nextjs-claude-starter

Next.js 16 starter with Claude Code superpowers built-in.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Next.js 16](https://img.shields.io/badge/Next.js-16-black)](https://nextjs.org)

**Part of cc-skills** | Works with [docpull](../docpull) for documentation

## What's Inside

### Next.js 16
- ✅ App Router
- ✅ Server Actions
- ✅ TypeScript
- ✅ Tailwind CSS
- ✅ Turbopack

### Claude Code (Pre-configured)
- 🤖 **7 Agents** - Security auditor, database architect, API builder, etc.
- 🎯 **6 Skills** - Next.js caching, streaming, App Router patterns
- ⚡ **5 Commands** - /sync-types, /clear-cache, /build-safe, etc.
- 🪝 **Hooks** - Pre-commit validation, auto-formatting
- ⚙️ **Settings** - Optimized for Next.js development

## Quick Start

```bash
# Clone this directory or use as template
git clone [repo-url] my-app
cd my-app

# Install dependencies
bun install

# Start development
bun dev

# Start Claude Code
claude
```

## Features

### AI-Powered Development

```bash
# Security audit
"Use security-auditor to check this auth flow"

# Database migrations
/db-migrate create add_user_settings

# Type safety
/sync-types

# Cache management
/clear-cache
```

### Next.js 16 + React 19

- Server Components by default
- Optimized caching strategies
- Streaming and Suspense
- Modern React patterns

## Pull Documentation

Use [docpull](../docpull) to fetch docs for your stack:

```bash
pip install docpull
docpull --source stripe --output ./docs/stripe
docpull --source supabase --output ./docs/supabase
```

Claude Code agents/skills will automatically reference the docs!

## Customization

### Remove Next.js-Specific Skills

```bash
rm -rf .claude/skills/next/
```

### Add Your Own Agents

```bash
# Create custom agent
echo "# My Agent

Agent instructions here..." > .claude/agents/my-agent.md
```

### Configure Settings

Edit `.claude/settings.json` to customize:
- Agent behaviors
- Hook automation
- Tool restrictions

## Structure

```
nextjs-claude-starter/
├── app/                # Next.js App Router
├── components/         # React components
├── lib/                # Utilities
├── public/             # Static assets
├── .claude/            # Claude Code config ⭐
│   ├── agents/        # AI assistants
│   ├── skills/        # Auto-invoked capabilities
│   ├── commands/      # Slash commands
│   ├── hooks/         # Event automation
│   └── settings.json  # Configuration
├── package.json
├── tsconfig.json
└── tailwind.config.ts
```

## Documentation

- [Full cc-skills docs](../../README.md)
- [Next.js Documentation](https://nextjs.org/docs)
- [Claude Code Documentation](https://code.claude.com/docs)

## Examples

### Use Security Auditor

```bash
# In Claude Code
"Use the security-auditor agent to check for vulnerabilities"
```

### Cache Optimization

```bash
# Just mention caching
"How should I cache this server component?"

# Claude automatically uses next-cache-architect skill
```

### Type Sync

```bash
/sync-types
# Syncs TypeScript types from Supabase
```

## License

MIT License - See LICENSE file

---

**Part of cc-skills** - Ship faster with AI.
