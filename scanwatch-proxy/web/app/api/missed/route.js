import { requireIdentity } from "../../../cursor/identity";
import { queueMissed } from "../../../lib/store";

export const dynamic = "force-dynamic";

export async function POST(request) {
  const identity = await requireIdentity(request);
  if (identity instanceof Response) return identity;
  let body = {};
  try {
    body = await request.json();
  } catch {
    body = {};
  }
  try {
    const result = await queueMissed(body.floor);
    return Response.json(result, {
      headers: { "cache-control": "private, no-store" },
    });
  } catch (error) {
    return Response.json(
      { error: error.message || "Could not record the floor" },
      { status: 400, headers: { "cache-control": "private, no-store" } },
    );
  }
}
