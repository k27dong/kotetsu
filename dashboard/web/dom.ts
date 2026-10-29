type Plain = string | number | boolean | null | undefined;
type EventName = keyof HTMLElementEventMap;
type Handlers = { [E in EventName as `on${E}`]?: (event: HTMLElementEventMap[E]) => unknown };
type AttributeValues<A> = {
  [Name in keyof A]: Name extends `on${infer E}` ? (E extends EventName ? unknown : never) : Plain; // other keys take plain values, so a handler can't hide under one
};
type Attributes<A extends object> = Handlers & AttributeValues<A> & A;
export type PlainAttributes = Record<string, Plain>;

type Child = Node | string | false | null | undefined;
export type Children = (Child | Child[])[];

export type Option<T extends string | number> = readonly [T, string];

const present = (child: Child): child is Node | string =>
  child !== null && child !== undefined && child !== false;

const isHandler = (value: unknown): value is EventListener => typeof value === "function";

export const h = <K extends keyof HTMLElementTagNameMap, A extends object = Record<never, never>>(
  tag: K,
  attributes?: Attributes<A>,
  ...children: Children
): HTMLElementTagNameMap[K] => {
  const element = document.createElement(tag);
  const entries: [string, unknown][] = Object.entries(attributes ?? {});
  for (const [name, value] of entries) {
    if (value === null || value === undefined || value === false) continue;
    if (isHandler(value)) element.addEventListener(name.slice(2), value);
    else if (name === "value" && "value" in element) element.value = String(value);
    else if (value === true) element.setAttribute(name, "");
    else element.setAttribute(name, String(value));
  }
  element.append(...children.flat().filter(present));
  return element;
};

export const select = <T extends string | number>(
  options: readonly Option<T>[],
  selected: T,
  onchange: (value: T) => void,
  attributes: PlainAttributes = {},
): HTMLSelectElement => {
  const element = h(
    "select",
    attributes,
    options.map(([value, label]) =>
      h("option", { value, selected: String(value) === String(selected) }, label),
    ),
  );
  element.addEventListener("change", () => {
    const chosen = options.find(([value]) => String(value) === element.value);
    if (chosen !== undefined) onchange(chosen[0]);
  });
  return element;
};

export const numbered = (
  count: number,
  suffix: string,
  names?: readonly string[],
): Option<number>[] =>
  Array.from({ length: count }, (_, index): Option<number> => [
    index + 1,
    names?.[index] ?? `${index + 1}${suffix}`,
  ]);
