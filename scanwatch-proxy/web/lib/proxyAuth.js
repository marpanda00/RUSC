import { timingSafeEqual } from "node:crypto";

export function proxyAuthorized(request) {
  const expected = process.env.SCANWATCH_SECRET?.trim() ?? "";
  if (!expected) {
    return !process.env.CURSOR_APP_ID;
  }
  const header = request.headers.get("authorization") || "";
  const got = header.startsWith("Bearer ") ? header.slice(7) : "";
  if (got.length !== expected.length) {
    return false;
  }
  return timingSafeEqual(Buffer.from(got), Buffer.from(expected));
}
