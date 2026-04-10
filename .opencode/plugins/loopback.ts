import type { Plugin } from "@opencode-ai/plugin"

export const LoopbackPlugin: Plugin = async ({ directory, gh }) => {
  const build = async () => {
    const proc = Bun.spawn(["xcodebuild", "-project", "Driver.xcodeproj", "-scheme", "Driver", "-configuration", "Release", "build", "CODE_SIGN_IDENTITY=-", "CODE_SIGNING_REQUIRED=NO"], {
      cwd: directory,
      stdout: "inherit",
      stderr: "inherit",
    })
    await proc.exited
    return proc.exitCode === 0
  }

  const openPR = async () => {
    const title = `[Driver] Build audio driver`
    const body = `Automated PR from OpenCode plugin`

    try {
      const proc = Bun.spawn(["gh", "pr", "create", "--title", title, "--body", body], {
        cwd: directory,
        stdout: "inherit",
        stderr: "inherit",
      })
      await proc.exited
      return proc.exitCode === 0
    } catch {
      return false
    }
  }

  return {
    event: async ({ event }) => {
      // Local hooks - bypass system reminders
      if (event.type === "session.status") {
        const props = (event as any).properties
        if (props?.status?.type === "idle") {
          await build()
        }
      }
    },
    commands: {
      build: {
        description: "Build audio driver",
        fn: build,
      },
      pr: {
        description: "Open PR with [Driver] prefix",
        fn: openPR,
      },
    },
  }
}